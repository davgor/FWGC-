// commit_gate: what tools/hooks/pre-commit and tools/hooks/commit-msg run. See ../README.md.
//
//   commit_gate pre-commit         the layer checks on the staged tree (fast; no review)
//   commit_gate commit-msg FILE    the layer checks, then the separation review of the staged
//                                  tree with this message, then the Separation-Review trailer
//   commit_gate review [-m MSG | -F FILE]
//                                  the review ahead of the commit, for a shell whose command
//                                  timeout is shorter than a review; commit-msg reuses it when
//                                  the tree and the message match
//   commit_gate check              the layer checks on the staged tree, printed
//   commit_gate audit [RANGE]      commits in RANGE whose trailer is missing, names another tree, or
//                                  has no review recorded on this machine behind it
//
// Exit status 0 lets git go on; anything else stops the commit. Nothing is skipped silently: a
// review that cannot run is a failed review.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "json.hpp"
#include "process.hpp"
#include "rules.hpp"

namespace fs = std::filesystem;
namespace json = layers::json;
namespace process = layers::process;

namespace {

const std::string kRules = "tools/check_layers/layers.txt";
const std::string kPrompt = "tools/red-team/separation.md";
const std::string kSchema = "tools/red-team/verdict.schema.json";
const std::string kTrailer = "Separation-Review";
constexpr std::size_t kDiffBudget = 400000;

// Paths whose change is a change to the rules themselves.
bool is_rule_file(const std::string& path) {
    auto starts = [&](const char* p) { return path.rfind(p, 0) == 0; };
    const std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    return starts("tools/check_layers/") || starts("tools/red-team/") || starts("tools/hooks/") ||
           path == "generator/api.txt" || name == "AGENTS.md" || name == "CLAUDE.md";
}

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

fs::path utf8_path(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string generic(const fs::path& p) {
    const std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string join(const std::vector<std::string>& parts, const std::string& sep = " ") {
    std::string out;
    for (std::size_t k = 0; k < parts.size(); ++k) out += (k ? sep : "") + parts[k];
    return out;
}

void say(const std::string& line) { std::cerr << "commit_gate: " << line << '\n'; }

class Git {
public:
    fs::path top;
    fs::path dir;

    process::Result raw(std::vector<std::string> args, const std::string& input = {}) const {
        args.insert(args.begin(), "git");
        return process::run(args, input, top);
    }
    std::string out(const std::vector<std::string>& args, const std::string& input = {}) const {
        const process::Result r = raw(args, input);
        if (r.exit_code != 0) throw std::runtime_error("git " + join(args) + " failed: " + trim(r.err));
        return r.out;
    }
    std::optional<std::string> show(const std::string& spec) const {
        const process::Result r = raw({"show", spec});
        if (r.exit_code != 0) return std::nullopt;
        return r.out;
    }
};

Git open_repo() {
    const process::Result top = process::run({"git", "rev-parse", "--show-toplevel"});
    if (top.exit_code != 0) throw std::runtime_error("not inside a git working tree: " + trim(top.err));
    Git g;
    g.top = utf8_path(trim(top.out));
    g.dir = utf8_path(trim(g.out({"rev-parse", "--absolute-git-dir"})));
    return g;
}

std::string staged_tree(const Git& g) { return trim(g.out({"write-tree"})); }

std::string head_commit(const Git& g) {
    const process::Result r = g.raw({"rev-parse", "--verify", "-q", "HEAD"});
    return r.exit_code == 0 ? trim(r.out) : "";
}

// A rule file as the last commit has it, or as staged when the last commit has none yet (the
// gate's own first commit). The second member names which.
std::optional<std::pair<std::string, std::string>> rule_file(const Git& g, const std::string& path) {
    if (auto t = g.show("HEAD:" + path)) return std::make_pair(*t, std::string("last commit"));
    if (auto t = g.show(":" + path)) return std::make_pair(*t, std::string("staged"));
    return std::nullopt;
}

// The staged tree, written out, for the checker to walk.
class Snapshot {
public:
    explicit Snapshot(const Git& g, const std::string& tree) {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        dir_ = fs::temp_directory_path() / ("fwgc_gate_" + tree.substr(0, 12) + "_" + std::to_string(tick % 1000000007));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        g.out({"checkout-index", "-a", "-f", "--prefix=" + generic(dir_) + "/"});
    }
    ~Snapshot() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    const fs::path& dir() const { return dir_; }

private:
    fs::path dir_;
};

// The layer checks under the last commit's rules and under the staged rules. A rule change
// therefore binds the commits after it, and a staged rule the tree already breaks is caught
// before it lands.
std::vector<layers::Finding> layer_check(const Git& g, const fs::path& snapshot) {
    std::vector<layers::Finding> out;
    auto config_failure = [&](const std::string& message) {
        out.push_back({"config", kRules, 0, message});
        return out;
    };
    layers::Rules staged;
    std::map<std::string, layers::ApiList> staged_api;
    try {
        staged = layers::parse_rules(layers::read_file(snapshot / utf8_path(kRules)), kRules + " (staged)");
        staged_api = layers::load_api_lists(staged, snapshot);
    } catch (const std::exception& e) {
        return config_failure(std::string("the staged rules: ") + e.what());
    }
    layers::Rules head = staged;
    std::map<std::string, layers::ApiList> head_api = staged_api;
    if (auto text = g.show("HEAD:" + kRules)) {
        try {
            head = layers::parse_rules(*text, kRules + " (last commit)");
            head_api.clear();
            for (const layers::Component& c : head.components) {
                if (c.api_list.empty()) continue;
                if (auto list = g.show("HEAD:" + c.api_list)) {
                    head_api.emplace(c.name, layers::parse_api_list(*list, c.api_list));
                } else if (auto s = staged_api.find(c.name); s != staged_api.end()) {
                    head_api.emplace(c.name, s->second);
                }
            }
        } catch (const std::exception& e) {
            return config_failure(std::string("the last commit's rules: ") + e.what());
        }
    }
    layers::Inputs under_head;
    under_head.root = snapshot;
    under_head.rules = head;
    under_head.consumer_api = head_api;
    under_head.own_api = staged_api;
    under_head.skip_ignored_dirs = false;
    layers::Inputs under_staged;
    under_staged.root = snapshot;
    under_staged.rules = staged;
    under_staged.consumer_api = staged_api;
    under_staged.own_api = staged_api;
    under_staged.skip_ignored_dirs = false;
    for (const layers::Inputs* in : {&under_head, &under_staged}) {
        const std::vector<layers::Finding> found = layers::check_tree(*in);
        out.insert(out.end(), found.begin(), found.end());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool report_layers(const std::vector<layers::Finding>& findings) {
    if (findings.empty()) {
        say("layer checks passed");
        return true;
    }
    for (const layers::Finding& f : findings) std::cerr << "  " << layers::format(f) << '\n';
    say(std::to_string(findings.size()) + " layer finding(s); the commit is stopped. tools/check_layers/README.md says what each rule is for.");
    return false;
}

// The message as the review saw it: comments, the scissors section and this gate's own
// trailer removed, trailing space trimmed. The cache key uses the same form.
std::string normalized_message(const std::string& raw) {
    std::istringstream in(raw);
    std::string line;
    std::vector<std::string> kept;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("# ------------------------ >8", 0) == 0) break;
        if (!line.empty() && line[0] == '#') continue;
        if (line.rfind(kTrailer + ":", 0) == 0) continue;
        kept.push_back(line);
    }
    return trim(join(kept, "\n"));
}

std::optional<fs::path> find_claude() {
    if (auto e = process::env("FWGC_CLAUDE")) {
        const fs::path p = utf8_path(*e);
        if (fs::is_regular_file(p)) return p;
    }
    if (auto p = process::find_on_path("claude")) return p;
    if (auto home = process::env("USERPROFILE")) {
        const fs::path p = utf8_path(*home) / ".local" / "bin" / "claude.exe";
        if (fs::is_regular_file(p)) return p;
    }
    if (auto appdata = process::env("APPDATA")) {
        // The desktop app's copies: %APPDATA%\Claude\claude-code\<version>\claude.exe, newest first.
        const fs::path root = utf8_path(*appdata) / "Claude" / "claude-code";
        std::error_code ec;
        std::vector<std::pair<std::vector<int>, fs::path>> found;
        for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
            const fs::path exe = e.path() / "claude.exe";
            if (!fs::is_regular_file(exe, ec)) continue;
            std::vector<int> version;
            std::istringstream parts(e.path().filename().string());
            std::string part;
            while (std::getline(parts, part, '.')) version.push_back(std::atoi(part.c_str()));
            found.emplace_back(version, exe);
        }
        if (!found.empty()) return std::max_element(found.begin(), found.end())->second;
    }
    return std::nullopt;
}

struct Verdict {
    bool ran = false;   // false: the review could not run, and `error` says why
    bool pass = false;
    std::string summary;
    json::Value findings = json::make_array();
    std::string error;
    std::string cost;
};

std::string describe_touched(const Git& g, const layers::Rules* rules, std::vector<std::string>& rule_files) {
    const std::string names = g.out({"diff", "--cached", "--name-only", "-z"});
    std::map<std::string, int> components;
    std::size_t start = 0;
    while (start < names.size()) {
        std::size_t end = names.find('\0', start);
        if (end == std::string::npos) end = names.size();
        const std::string path = names.substr(start, end - start);
        start = end + 1;
        if (path.empty()) continue;
        if (is_rule_file(path)) rule_files.push_back(path);
        const std::size_t slash = path.find('/');
        std::string label = "repository root";
        if (slash != std::string::npos) {
            const std::string top = path.substr(0, slash);
            const layers::Component* c = rules ? rules->by_folder(top) : nullptr;
            label = c ? c->name + " (" + (c->kind == layers::Kind::Runtime ? "runtime" : c->kind == layers::Kind::Devtool ? "dev tool" : "assets") + ")"
                      : top + " (undeclared)";
        }
        ++components[label];
    }
    std::vector<std::string> parts;
    for (const auto& [label, count] : components) parts.push_back(label + ": " + std::to_string(count) + " file(s)");
    return parts.empty() ? "none" : join(parts, "; ");
}

Verdict run_review(const Git& g, const std::string& tree, const std::string& message) {
    Verdict v;
    const std::string diff = g.out({"diff", "--cached", "--no-color", "--no-ext-diff", "-M"});
    if (trim(diff).empty()) {
        v.ran = true;
        v.pass = true;
        v.summary = "nothing staged differs from the last commit";
        return v;
    }
    const auto prompt = rule_file(g, kPrompt);
    const auto schema = rule_file(g, kSchema);
    if (!prompt || !schema) {
        v.error = "the red-team prompt or schema is missing (" + kPrompt + ", " + kSchema + ")";
        return v;
    }
    std::string schema_compact;
    try {
        schema_compact = json::dump(json::parse(schema->first));
    } catch (const std::exception& e) {
        v.error = std::string("the verdict schema does not parse: ") + e.what();
        return v;
    }
    const std::optional<fs::path> claude = find_claude();
    if (!claude) {
        v.error = "the claude CLI was not found (FWGC_CLAUDE, PATH, %USERPROFILE%\\.local\\bin, the desktop app); "
                  "install it and log in once with `claude`";
        return v;
    }

    std::optional<layers::Rules> rules;
    if (auto r = rule_file(g, kRules)) {
        try {
            rules = layers::parse_rules(r->first, kRules);
        } catch (const std::exception&) {
        }
    }
    std::vector<std::string> rule_files;
    const std::string touched = describe_touched(g, rules ? &*rules : nullptr, rule_files);
    const std::string head = head_commit(g);
    const std::string agent = process::env("FWGC_AGENT").value_or("");
    const std::string stat = g.out({"diff", "--cached", "--stat=200"});

    std::ostringstream packet;
    packet << "# FWGC++ separation review request\n\n"
           << "- Staged tree: " << tree << "\n"
           << "- Parent commit: " << (head.empty() ? "(none: the first commit)" : head) << "\n"
           << "- Declared agent (FWGC_AGENT): " << (agent.empty() ? "unset" : agent) << "\n"
           << "- Components touched: " << touched << "\n"
           << "- Rule files changed: " << (rule_files.empty() ? "none" : join(rule_files, ", ")) << "\n"
           << "- Red-team prompt and schema: as of the " << prompt->second << "\n"
           << "- Mechanical checks (tools/check_layers, under the last commit's rules and the staged rules): passed\n\n"
           << "## Commit message\n\n" << message << "\n\n"
           << "## git diff --cached --stat\n\n" << stat << "\n"
           << "## git diff --cached\n\n";
    if (diff.size() <= kDiffBudget) {
        packet << diff;
    } else {
        packet << diff.substr(0, kDiffBudget) << "\n[the diff is truncated at " << kDiffBudget
               << " bytes: read the rest with `git diff --cached -- <path>`]\n";
    }

    const fs::path prompt_file = fs::temp_directory_path() / ("fwgc_gate_prompt_" + tree.substr(0, 12) + ".md");
    {
        std::ofstream f(prompt_file, std::ios::binary);
        f << prompt->first;
    }
    std::vector<std::string> args = {generic(*claude),
                                     "-p",
                                     "Review the staged commit described on standard input, as your system prompt says. "
                                     "Give your verdict only through the structured output.",
                                     "--append-system-prompt-file",
                                     generic(prompt_file),
                                     "--tools",
                                     "Read,Grep,Glob,Bash",
                                     "--allowedTools",
                                     "Read",
                                     "Grep",
                                     "Glob",
                                     "Bash(git diff *)",
                                     "Bash(git show *)",
                                     "Bash(git log *)",
                                     "Bash(git grep *)",
                                     "Bash(git ls-files *)",
                                     "--permission-mode",
                                     "dontAsk",
                                     "--restricted",
                                     "--strict-mcp-config",
                                     "--no-session-persistence",
                                     "--output-format",
                                     "json",
                                     "--json-schema",
                                     schema_compact,
                                     "--max-budget-usd",
                                     process::env("FWGC_REVIEW_BUDGET").value_or("5")};
    if (auto model = process::env("FWGC_REVIEW_MODEL")) {
        args.push_back("--model");
        args.push_back(*model);
    }
    const unsigned minutes = static_cast<unsigned>(std::max(1, std::atoi(process::env("FWGC_REVIEW_TIMEOUT_MIN").value_or("20").c_str())));
    say("reviewing tree " + tree.substr(0, 12) + " with " + generic(*claude) + " (up to " + std::to_string(minutes) + " min)");
    process::Result r;
    try {
        // Nothing of the committing session reaches the reviewer: every CLAUDE* and ANTHROPIC*
        // variable is dropped, and the CLI runs on its own login.
        r = process::run(args, packet.str(), g.top, minutes * 60000u, {"CLAUDE", "ANTHROPIC"});
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove(prompt_file, ec);
        v.error = e.what();
        return v;
    }
    std::error_code ec;
    fs::remove(prompt_file, ec);
    if (r.timed_out) {
        v.error = "the review timed out after " + std::to_string(minutes) + " minutes";
        return v;
    }
    json::Value result;
    try {
        result = json::parse(r.out);
    } catch (const std::exception& e) {
        v.error = "the reviewer's output is not JSON (" + std::string(e.what()) + "); exit " + std::to_string(r.exit_code) +
                  "; stderr: " + trim(r.err).substr(0, 600) + "; stdout: " + trim(r.out).substr(0, 600);
        return v;
    }
    if (const json::Value* c = result.get("total_cost_usd")) v.cost = c->text;
    const json::Value* is_error = result.get("is_error");
    const json::Value* subtype = result.get("subtype");
    const json::Value* so = result.get("structured_output");
    if ((is_error && is_error->boolean) || !subtype || subtype->text != "success" || !so || !so->is(json::Value::Type::Object)) {
        std::string errors;
        if (const json::Value* e = result.get("errors")) errors = json::dump(*e);
        v.error = "the review did not finish: subtype " + (subtype ? subtype->text : std::string("?")) + " " + errors;
        return v;
    }
    const json::Value* verdict = so->get("verdict");
    const json::Value* summary = so->get("summary");
    const json::Value* findings = so->get("findings");
    if (!verdict || !findings || !findings->is(json::Value::Type::Array)) {
        v.error = "the structured output lacks a verdict or findings: " + json::dump(*so).substr(0, 600);
        return v;
    }
    v.ran = true;
    v.summary = summary ? summary->text : "";
    v.findings = *findings;
    bool blocked = verdict->text != "PASS";
    for (const json::Value& f : findings->items)
        if (const json::Value* s = f.get("severity"); s && s->text == "block") blocked = true;
    v.pass = !blocked;
    return v;
}

void print_verdict(const Verdict& v) {
    if (!v.ran) {
        say("the review could not run: " + v.error);
        say("a review that cannot run stops the commit; fix the cause, or ask the owner");
        return;
    }
    say(std::string("review ") + (v.pass ? "PASSED" : "FAILED") + (v.cost.empty() ? "" : " (cost " + v.cost + " USD)") + ": " + v.summary);
    for (const json::Value& f : v.findings.items) {
        auto field = [&](const char* k) {
            const json::Value* x = f.get(k);
            return x ? x->text : std::string();
        };
        std::cerr << "  [" << field("severity") << " " << field("rule") << "] " << field("file") << ":" << field("line") << "\n"
                  << "      evidence: " << field("evidence") << "\n"
                  << "      why: " << field("why") << "\n";
    }
}

fs::path cache_file(const Git& g, const std::string& tree, const std::string& message_id) {
    return g.dir / "fwgc-review" / (tree + "-" + message_id.substr(0, 16) + ".json");
}

std::string message_id(const Git& g, const std::string& message) {
    return trim(g.out({"hash-object", "--stdin"}, message));
}

std::string prompt_id(const Git& g) {
    for (const std::string& spec : {"HEAD:" + kPrompt, ":" + kPrompt}) {
        const process::Result r = g.raw({"rev-parse", "--verify", "-q", spec});
        if (r.exit_code == 0) return trim(r.out);
    }
    return "";
}

std::optional<Verdict> cached(const Git& g, const std::string& tree, const std::string& message) {
    const fs::path file = cache_file(g, tree, message_id(g, message));
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    try {
        const json::Value c = json::parse(layers::read_file(file));
        const json::Value* prompt = c.get("prompt_blob");
        if (!prompt || prompt->text != prompt_id(g)) return std::nullopt;  // the red team changed since
        Verdict v;
        v.ran = true;
        v.pass = c.get("verdict") && c.get("verdict")->text == "PASS";
        v.summary = c.get("summary") ? c.get("summary")->text : "";
        if (const json::Value* f = c.get("findings")) v.findings = *f;
        v.cost = "0 (cached)";
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void store(const Git& g, const std::string& tree, const std::string& message, const Verdict& v) {
    if (!v.ran) return;
    json::Value c = json::make_object();
    json::set(c, "tree", json::make_string(tree));
    json::set(c, "message_id", json::make_string(message_id(g, message)));
    json::set(c, "prompt_blob", json::make_string(prompt_id(g)));
    json::set(c, "verdict", json::make_string(v.pass ? "PASS" : "FAIL"));
    json::set(c, "summary", json::make_string(v.summary));
    json::set(c, "findings", v.findings);
    json::set(c, "agent", json::make_string(process::env("FWGC_AGENT").value_or("")));
    json::set(c, "cost_usd", json::make_string(v.cost));
    json::set(c, "reviewed_at",
              json::make_string(std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()))));
    const fs::path file = cache_file(g, tree, message_id(g, message));
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << json::dump(c) << '\n';
}

// Checks, then the review (from the cache when this tree and message were reviewed under the
// current red-team prompt). True when the commit may go on.
bool check_and_review(const Git& g, const std::string& message) {
    const std::string tree = staged_tree(g);
    {
        Snapshot snapshot(g, tree);
        if (!report_layers(layer_check(g, snapshot.dir()))) return false;
    }
    std::optional<Verdict> v = cached(g, tree, message);
    if (v) {
        say("reusing the review of tree " + tree.substr(0, 12) + " with this message");
    } else {
        v = run_review(g, tree, message);
        store(g, tree, message, *v);
    }
    print_verdict(*v);
    return v->ran && v->pass;
}

int pre_commit() {
    const Git g = open_repo();
    const std::string tree = staged_tree(g);
    Snapshot snapshot(g, tree);
    return report_layers(layer_check(g, snapshot.dir())) ? 0 : 1;
}

int commit_msg(const std::string& file) {
    const Git g = open_repo();
    const std::string message = normalized_message(layers::read_file(utf8_path(file)));
    if (message.empty()) return 0;  // git aborts an empty message itself
    if (!check_and_review(g, message)) return 1;
    g.out({"interpret-trailers", "--in-place", "--if-exists", "replace", "--trailer",
           kTrailer + ": PASS " + staged_tree(g), file});
    return 0;
}

int review(const std::vector<std::string>& args) {
    std::string message;
    for (std::size_t k = 0; k < args.size(); ++k) {
        if (args[k] == "-m" && k + 1 < args.size()) message = args[++k];
        else if (args[k] == "-F" && k + 1 < args.size()) message = layers::read_file(utf8_path(args[++k]));
        else throw std::runtime_error("review takes -m MESSAGE or -F FILE");
    }
    message = normalized_message(message);
    if (message.empty()) throw std::runtime_error("review needs the commit message (-m or -F) it will be committed with");
    const Git g = open_repo();
    return check_and_review(g, message) ? 0 : 1;
}

int check() {
    const Git g = open_repo();
    const std::string tree = staged_tree(g);
    Snapshot snapshot(g, tree);
    return report_layers(layer_check(g, snapshot.dir())) ? 0 : 1;
}

// Whether this machine recorded a passing review of `tree`. A trailer typed by hand has none.
bool has_record(const Git& g, const std::string& tree) {
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(g.dir / "fwgc-review", ec)) {
        if (e.path().filename().string().rfind(tree + "-", 0) != 0) continue;
        try {
            const json::Value c = json::parse(layers::read_file(e.path()));
            if (c.get("verdict") && c.get("verdict")->text == "PASS") return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

int audit(std::string range) {
    const Git g = open_repo();
    if (range.empty()) {
        const std::string adds = trim(g.out({"log", "--diff-filter=A", "--format=%H", "--", "tools/hooks/commit-msg"}));
        if (adds.empty()) {
            say("the gate is not in the history yet; nothing to audit");
            return 0;
        }
        range = adds.substr(adds.rfind('\n') == std::string::npos ? 0 : adds.rfind('\n') + 1) + "..HEAD";
    }
    const std::string log = g.out({"log", "--format=%H%x1f%T%x1f%B%x1e", range});
    int bad = 0, total = 0;
    std::size_t start = 0;
    while (start < log.size()) {
        std::size_t end = log.find('\x1e', start);
        if (end == std::string::npos) end = log.size();
        const std::string record = trim(log.substr(start, end - start));
        start = end + 1;
        if (record.empty()) continue;
        const std::size_t a = record.find('\x1f'), b = record.find('\x1f', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        const std::string commit = record.substr(0, a), tree = record.substr(a + 1, b - a - 1), body = record.substr(b + 1);
        ++total;
        std::string reviewed;
        std::istringstream lines(body);
        std::string line;
        while (std::getline(lines, line))
            if (line.rfind(kTrailer + ": PASS ", 0) == 0) reviewed = trim(line.substr(kTrailer.size() + 7));
        if (reviewed.empty()) {
            std::cout << commit.substr(0, 12) << " has no " << kTrailer << " trailer\n";
            ++bad;
        } else if (reviewed != tree) {
            std::cout << commit.substr(0, 12) << " was reviewed as tree " << reviewed.substr(0, 12) << " but is tree " << tree.substr(0, 12) << "\n";
            ++bad;
        } else if (!has_record(g, tree)) {
            std::cout << commit.substr(0, 12) << " carries a trailer, but no review of its tree is recorded in .git/fwgc-review "
                      << "(reviewed on another machine, or the trailer was written by hand)\n";
            ++bad;
        }
    }
    std::cout << total << " commit(s) in " << range << ", " << bad << " without a matching review\n";
    return bad == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.empty()) throw std::runtime_error("usage: commit_gate pre-commit | commit-msg FILE | review -m MSG | check | audit [RANGE]");
        const std::string& command = args[0];
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (command == "pre-commit") return pre_commit();
        if (command == "commit-msg" && rest.size() == 1) return commit_msg(rest[0]);
        if (command == "review") return review(rest);
        if (command == "check") return check();
        if (command == "audit") return audit(rest.empty() ? "" : rest[0]);
        throw std::runtime_error("unknown command '" + command + "'");
    } catch (const std::exception& e) {
        say(e.what());
        say("the gate could not finish, so the commit is stopped");
        return 2;
    }
}
