#include "check.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <tuple>

#include "json.hpp"
#include "lex.hpp"

namespace fs = std::filesystem;

namespace layers {

bool operator<(const Finding& a, const Finding& b) {
    return std::tie(a.path, a.line, a.rule, a.message) < std::tie(b.path, b.line, b.rule, b.message);
}

bool operator==(const Finding& a, const Finding& b) {
    return std::tie(a.path, a.line, a.rule, a.message) == std::tie(b.path, b.line, b.rule, b.message);
}

std::string format(const Finding& f) {
    std::string where = f.path;
    if (f.line > 0) where += ":" + std::to_string(f.line);
    return where + ": [" + f.rule + "] " + f.message;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::map<std::string, ApiList> load_api_lists(const Rules& rules, const fs::path& root) {
    std::map<std::string, ApiList> out;
    for (const Component& c : rules.components) {
        if (c.api_list.empty()) continue;
        const fs::path path = root / c.api_list;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) continue;
        out.emplace(c.name, parse_api_list(read_file(path), c.api_list));
    }
    return out;
}

namespace {

std::string generic(const fs::path& p) {
    const std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string filename_of(const std::string& rel) {
    const std::size_t slash = rel.rfind('/');
    return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

std::string extension_of(const std::string& rel) {
    const std::string name = filename_of(rel);
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return "";
    return lower(name.substr(dot));
}

bool is_cpp(const std::string& ext) {
    static const std::set<std::string> kExt = {".cpp", ".cc",  ".cxx", ".c",   ".hpp", ".hh",  ".hxx",
                                               ".h",   ".ipp", ".inl", ".tpp", ".ixx", ".cppm"};
    return kExt.count(ext) > 0;
}

bool is_cmake(const std::string& rel) { return filename_of(rel) == "CMakeLists.txt" || extension_of(rel) == ".cmake"; }

bool is_script_text(const std::string& ext) {
    static const std::set<std::string> kExt = {".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx", ".html", ".htm"};
    return kExt.count(ext) > 0;
}

std::string trim_slash(std::string p) {
    while (!p.empty() && p.back() == '/') p.pop_back();
    return p;
}

bool under(const std::string& rel, const std::string& prefix_raw) {
    const std::string prefix = trim_slash(prefix_raw);
    return rel == prefix || (rel.size() > prefix.size() && rel.compare(0, prefix.size(), prefix) == 0 &&
                             rel[prefix.size()] == '/');
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

fs::path native(const std::string& generic_path) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(generic_path.data()), generic_path.size()));
}

// A deny-include pattern: 'curl/' matches any include under curl/, 'winhttp.h' matches that
// header at any depth. Both sides lower case.
bool matches_header(const std::string& path, const std::string& pattern) {
    if (pattern.back() == '/') return starts_with(path, pattern);
    if (path == pattern) return true;
    return path.size() > pattern.size() && path.compare(path.size() - pattern.size(), pattern.size(), pattern) == 0 &&
           path[path.size() - pattern.size() - 1] == '/';
}

// A function or namespace name, as the repository spells them: snake_case. UpperCamel is a
// type and kUpperCamel a constant, and both are free to read.
bool lowercase_name(const std::string& name) {
    if (name.size() > 1 && name[0] == 'k' && std::isupper(static_cast<unsigned char>(name[1]))) return false;
    return !name.empty() && (name[0] == '_' || std::islower(static_cast<unsigned char>(name[0])));
}

struct Source {
    std::string rel;
    const Component* component = nullptr;
    std::string text;
    Stripped stripped;
    std::vector<Token> tokens;
};

class Checker {
public:
    explicit Checker(const Inputs& in) : in_(in) {}

    std::vector<Finding> run() {
        walk();
        check_top_level();
        if (!in_.rules.dependency_manifest.empty()) check_manifest();
        load_sources();
        for (const Component& c : in_.rules.components)
            if (!c.api_list.empty()) check_api_list(c);
        for (const std::string& rel : files_) check_file(rel);
        std::sort(out_.begin(), out_.end());
        out_.erase(std::unique(out_.begin(), out_.end()), out_.end());
        return out_;
    }

private:
    const Inputs& in_;
    std::vector<Finding> out_;
    std::vector<std::string> files_;
    std::set<std::string> top_dirs_;
    std::map<std::string, Source> sources_;                         // C++ files by rel path
    std::map<std::string, std::set<std::string>> self_declared_;    // component -> names it declares
    std::map<std::string, std::set<std::string>> internal_;         // component -> ns-scope functions
    std::map<std::string, std::set<std::string>> forwarded_;        // api forwarder -> declarations
    std::set<std::string> build_dependencies_;                      // manifest names a build may fetch

    void add(std::string rule, std::string path, int line, std::string message) {
        out_.push_back({std::move(rule), std::move(path), line, std::move(message)});
    }

    bool skipped(const std::string& rel) const {
        for (const std::string& p : in_.rules.skip_paths)
            if (under(rel, p)) return true;
        return false;
    }

    const Component* component_of(const std::string& rel) const {
        return in_.rules.by_folder(rel.substr(0, rel.find('/')));
    }

    void walk() {
        std::error_code ec;
        fs::recursive_directory_iterator it(in_.root, fs::directory_options::skip_permission_denied, ec);
        if (ec) throw std::runtime_error("cannot walk " + in_.root.string() + ": " + ec.message());
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                add("walk", ".", 0, "the walk stopped early: " + ec.message());
                break;
            }
            const std::string rel = generic(it->path().lexically_relative(in_.root));
            const std::string first = rel.substr(0, rel.find('/'));
            std::error_code type_ec;
            const bool dir = it->is_directory(type_ec);
            // In a snapshot, a top-level name that is also build output (build/, out/) is not
            // skipped: nothing untracked is there, so a staged folder by that name is real.
            const bool build_name = in_.rules.ignore_dirs.count(first) > 0;
            if (it.depth() == 0 && in_.rules.ignore_top.count(first) && (in_.skip_ignored_dirs || !build_name)) {
                if (dir) it.disable_recursion_pending();
                continue;
            }
            if (dir) {
                if ((in_.skip_ignored_dirs && in_.rules.ignore_dirs.count(generic(it->path().filename()))) || skipped(rel)) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it.depth() == 0) top_dirs_.insert(first);
                continue;
            }
            if (skipped(rel)) continue;
            files_.push_back(rel);
        }
        std::sort(files_.begin(), files_.end());
    }

    void check_top_level() {
        for (const std::string& d : top_dirs_) {
            if (in_.rules.forbid_top.count(d))
                add("top-level", d + "/", 0,
                    "a shared grab-bag folder is forbidden: what two components share belongs to the lower one");
            else if (!in_.rules.by_folder(d))
                add("top-level", d + "/", 0,
                    "undeclared top-level folder: declare it as a component in tools/check_layers/layers.txt, "
                    "in a commit of its own, before adding it");
        }
    }

    // The dependency manifest: every entry says what it is, where it comes from, exactly which bytes
    // (size and hash), its licence, who uses it, whether it ships, and when the owner approved it.
    // A problem is reported at the line of the entry's name.
    void check_manifest() {
        using json::Value;
        const std::string& rel = in_.rules.dependency_manifest;
        std::error_code ec;
        if (!in_.rules.dependency_helper.empty() && !fs::is_regular_file(in_.root / native(in_.rules.dependency_helper), ec))
            add("deps", in_.rules.dependency_helper, 0, "the fetch helper layers.txt names is missing");
        std::string text;
        try {
            text = read_file(in_.root / native(rel));
        } catch (const std::exception&) {
            add("deps", rel, 0, "the dependency manifest layers.txt names is missing");
            return;
        }
        Value doc;
        try {
            doc = json::parse(text);
        } catch (const json::Error& e) {
            add("deps", rel, 0, std::string("not JSON: ") + e.what());
            return;
        }
        const Value* list = doc.get("dependencies");
        if (list == nullptr || !list->is(Value::Type::Array)) {
            add("deps", rel, 0, "names no 'dependencies' list");
            return;
        }
        std::set<std::string> names;
        std::map<std::string, int> seen;  // entries so far by name: the nth is at the nth mention
        for (const Value& entry : list->items) check_dependency(rel, text, entry, names, seen);
    }

    static bool valid_hash(const std::string& hash) {
        static const std::map<std::string, std::size_t> kHexDigits = {
            {"SHA256", 64}, {"SHA384", 96}, {"SHA512", 128}, {"SHA3_256", 64}, {"SHA3_384", 96}, {"SHA3_512", 128}};
        const std::size_t eq = hash.find('=');
        if (eq == std::string::npos) return false;
        const auto algorithm = kHexDigits.find(hash.substr(0, eq));
        const std::string hex = hash.substr(eq + 1);
        return algorithm != kHexDigits.end() && hex.size() == algorithm->second &&
               std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    }

    static bool positive_integer(const json::Value* v) {
        return v != nullptr && v->is(json::Value::Type::Number) && !v->text.empty() &&
               std::all_of(v->text.begin(), v->text.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
               v->text.find_first_not_of('0') != std::string::npos;
    }

    void check_dependency(const std::string& rel, const std::string& text, const json::Value& entry, std::set<std::string>& names,
                          std::map<std::string, int>& seen) {
        using json::Value;
        const auto str = [&](const char* key) {
            const Value* v = entry.get(key);
            return v != nullptr && v->is(Value::Type::String) ? v->text : std::string();
        };
        const std::string name = str("name");
        // The line of this entry's name: its nth mention, when it is the nth entry by that name.
        int line = 0;
        if (!name.empty()) {
            const int nth = seen[name]++;
            for (const std::string& spelled : {"\"name\": \"" + name + "\"", "\"name\":\"" + name + "\""}) {
                std::size_t at = text.find(spelled);
                for (int k = 0; k < nth && at != std::string::npos; ++k) at = text.find(spelled, at + 1);
                if (line == 0 && at != std::string::npos) line = line_of(text, at);
            }
        }
        const std::string who = name.empty() ? std::string("an entry") : "'" + name + "'";
        const auto problem = [&](const std::string& message) { add("deps", rel, line, who + ": " + message); };
        if (!entry.is(Value::Type::Object)) {
            problem("is not an object");
            return;
        }
        const bool name_ok = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        });
        if (!name_ok) problem("needs a 'name' of lower-case letters, digits and '_'");
        else if (!names.insert(name).second) problem("is named twice");
        for (const char* key : {"what", "version", "kind", "fetch", "licence", "obligations", "ships", "approved"})
            if (str(key).empty()) problem(std::string("needs '") + key + "'");
        static const std::set<std::string> kKinds = {"source", "model", "python", "installer"};
        static const std::set<std::string> kFetches = {"build", "tool", "pip", "manual"};
        static const std::set<std::string> kShips = {"yes", "no", "planned"};
        if (!str("kind").empty() && !kKinds.count(str("kind"))) problem("kind is source, model, python or installer");
        if (!str("fetch").empty() && !kFetches.count(str("fetch"))) problem("fetch is build, tool, pip or manual");
        if (!str("ships").empty() && !kShips.count(str("ships"))) problem("ships is yes, no or planned");
        // Approved on a day: the owner's approval of the file, its source and its size.
        const std::string approved = str("approved");
        const bool dated = approved.size() >= 10 && approved[4] == '-' && approved[7] == '-' &&
                           std::all_of(approved.begin(), approved.begin() + 10, [](char c) { return c == '-' || std::isdigit(static_cast<unsigned char>(c)); });
        if (!approved.empty() && !dated) problem("'approved' opens with the day the owner approved it (YYYY-MM-DD)");
        const Value* users = entry.get("used_by");
        if (users == nullptr || !users->is(Value::Type::Array) || users->items.empty()) {
            problem("needs 'used_by', the components that use it");
        } else {
            for (const Value& u : users->items)
                if (!u.is(Value::Type::String) || !in_.rules.find(u.text))
                    problem("'used_by' names '" + u.text + "', which layers.txt declares no component by");
        }
        const std::string fetch = str("fetch");
        if (fetch == "build" || fetch == "tool") {
            if (str("url").rfind("https://", 0) != 0) problem("needs an https 'url'");
            const Value* files = entry.get("files");
            const auto check_file_entry = [&](const Value& f, const std::string& where) {
                const Value* file = f.get("file");
                if (file == nullptr || !file->is(Value::Type::String) || file->text.empty()) problem(where + "needs 'file'");
                if (!positive_integer(f.get("bytes"))) problem(where + "needs 'bytes', its exact size");
                const Value* hash = f.get("hash");
                if (hash == nullptr || !hash->is(Value::Type::String) || !valid_hash(hash->text))
                    problem(where + "needs 'hash' as ALGORITHM=hex (SHA256, SHA3_256, ...), in full");
            };
            if (files != nullptr) {
                if (!files->is(Value::Type::Array) || files->items.empty()) problem("'files' is a list of files");
                else
                    for (const Value& f : files->items) check_file_entry(f, "a file of its 'files' ");
            } else {
                check_file_entry(entry, "");
            }
            if (fetch == "tool" && str("dest").empty()) problem("a tool fetch needs 'dest', where in the tree it goes");
            if (fetch == "build" && name_ok) build_dependencies_.insert(name);
        } else if (fetch == "pip") {
            // Every requirement in the lock carries a hash, so pip --require-hashes installs it.
            const std::string lock = str("lock");
            std::string locked;
            try {
                locked = lock.empty() ? std::string() : read_file(in_.root / native(lock));
            } catch (const std::exception&) {
                problem("its lock file '" + lock + "' is missing");
            }
            if (lock.empty()) problem("a pip fetch needs 'lock', its hash-locked requirements file");
            std::istringstream lines(locked);
            std::string requirement;
            while (std::getline(lines, requirement)) {
                const std::size_t first = requirement.find_first_not_of(" \t\r");
                if (first == std::string::npos || requirement[first] == '#' || requirement[first] == '-') continue;
                if (requirement.find("--hash=") == std::string::npos)
                    problem("'" + lock + "' holds a requirement without a hash: " + requirement.substr(first, 60));
            }
        } else if (fetch == "manual") {
            if (str("url").empty()) problem("needs 'url', where a person gets it");
        }
    }

    void load_sources() {
        for (const std::string& rel : files_) {
            const Component* c = component_of(rel);
            if (!c || !is_cpp(extension_of(rel))) continue;
            Source s;
            s.rel = rel;
            s.component = c;
            try {
                s.text = read_file(in_.root / native(rel));
            } catch (const std::exception& e) {
                add("walk", rel, 0, e.what());
                continue;
            }
            s.stripped = strip_cpp(s.text);
            s.tokens = tokenize(s.stripped.code);
            const std::set<std::string> declared = declared_functions(s.tokens, false);
            self_declared_[c->name].insert(declared.begin(), declared.end());
            sources_.emplace(rel, std::move(s));
        }
    }

    // Namespace-scope functions declared anywhere under a component's include dir, the api
    // forwarders aside.
    const std::set<std::string>& internal_functions(const Component& c) {
        auto found = internal_.find(c.name);
        if (found != internal_.end()) return found->second;
        std::set<std::string>& names = internal_[c.name];
        for (const auto& [rel, s] : sources_) {
            if (!under(rel, c.include_dir) || (!c.api_headers.empty() && under(rel, c.api_headers))) continue;
            const std::set<std::string> declared = declared_functions(s.tokens, true);
            names.insert(declared.begin(), declared.end());
        }
        return names;
    }

    const std::set<std::string>& forwarded_declarations(const Component& c, const std::string& forwarder) {
        auto found = forwarded_.find(forwarder);
        if (found != forwarded_.end()) return found->second;
        std::set<std::string>& names = forwarded_[forwarder];
        auto source = sources_.find(forwarder);
        if (source == sources_.end()) return names;
        for (const Include& inc : includes_of(source->second.stripped.no_comments)) {
            const bool ours = std::any_of(c.include_roots.begin(), c.include_roots.end(),
                                          [&](const std::string& root) { return starts_with(inc.path, root); });
            if (!ours) continue;
            const std::string target = trim_slash(c.include_dir) + "/" + inc.path;
            auto t = sources_.find(target);
            if (t == sources_.end()) {
                add("api-list", forwarder, inc.line, "forwards " + inc.path + ", which does not exist under " + c.include_dir);
                continue;
            }
            const std::set<std::string> declared = declared_functions(t->second.tokens, true);
            names.insert(declared.begin(), declared.end());
        }
        return names;
    }

    void check_api_list(const Component& c) {
        auto list = in_.own_api.find(c.name);
        if (list == in_.own_api.end()) {
            add("api-list", c.api_list, 0, c.name + " declares an api-list in layers.txt, and it is missing");
            return;
        }
        for (const ApiEntry& e : list->second.entries) {
            for (const std::string& h : e.headers) {
                const std::string forwarder = trim_slash(c.api_headers) + "/" + h + ".hpp";
                if (!sources_.count(forwarder)) {
                    add("api-list", list->second.source, e.line, "names header '" + h + "', but " + forwarder + " does not exist");
                    continue;
                }
                if (!forwarded_declarations(c, forwarder).count(e.function))
                    add("api-list", list->second.source, e.line,
                        e.function + " is listed under '" + h + "', but no header " + forwarder +
                            " includes declares it at namespace scope");
            }
        }
    }

    void check_file(const std::string& rel) {
        const Component* c = component_of(rel);
        if (!c) return;
        const std::string ext = extension_of(rel);
        if (c->cpp_only && in_.rules.non_cpp_ext.count(ext)) {
            const bool excepted = std::any_of(c->cpp_only_except.begin(), c->cpp_only_except.end(),
                                              [&](const std::string& p) { return under(rel, p); });
            if (!excepted)
                add("lang", rel, 0, c->name + " is a runtime component and C++ only; " + ext +
                                        " source belongs in a declared dev-tool exception");
        }
        if (c->no_code && (is_cpp(ext) || in_.rules.non_cpp_ext.count(ext) || is_cmake(rel)))
            add("no-code", rel, 0, c->name + " holds documents and assets, not code");
        auto source = sources_.find(rel);
        if (source != sources_.end()) check_cpp(source->second);
        if (is_cmake(rel)) check_cmake(*c, rel);
        if (!c->deny_path_text.empty() && is_script_text(ext)) check_path_text(*c, rel);
    }

    void check_cpp(const Source& s) {
        const Component& c = *s.component;
        check_includes(s);
        check_namespaces(s);
        for (const std::string& dep : c.depends) {
            const Component* d = in_.rules.find(dep);
            if (d && !d->api_list.empty()) check_consumer(s, *d);
        }
        if (!c.deny_words.empty()) check_vocabulary(s);
    }

    void check_includes(const Source& s) {
        const Component& c = *s.component;
        for (const Include& inc : includes_of(s.stripped.no_comments)) {
            const std::string path = lower(inc.path);
            if (c.kind == Kind::Runtime) {
                for (const std::string& pattern : in_.rules.runtime_deny_includes) {
                    if (matches_header(path, pattern))
                        add("http", s.rel, inc.line,
                            "includes " + inc.path + ": runtime components ship fully local and link no network library");
                }
            }
            const Component* owner = nullptr;
            std::size_t best = 0;
            for (const Component& o : in_.rules.components)
                for (const std::string& root : o.include_roots)
                    if (starts_with(inc.path, root) && root.size() > best) {
                        owner = &o;
                        best = root.size();
                    }
            if (owner && owner != &c) {
                if (!c.depends.count(owner->name)) {
                    add("edge", s.rel, inc.line,
                        c.name + " includes " + inc.path + " from " + owner->name + ", which it does not depend on");
                } else if (!owner->public_includes.empty()) {
                    const bool is_public = std::any_of(owner->public_includes.begin(), owner->public_includes.end(),
                                                       [&](const std::string& p) { return starts_with(inc.path, p); });
                    if (!is_public)
                        add("api-include", s.rel, inc.line,
                            inc.path + " is internal to " + owner->name + "; include its public headers (" +
                                owner->public_includes.front() + "...) instead");
                }
            }
            if (!inc.angled && inc.path.find("..") != std::string::npos) {
                const std::string resolved = generic((fs::path(s.rel).parent_path() / native(inc.path)).lexically_normal());
                if (starts_with(resolved, "..") || resolved.substr(0, resolved.find('/')) != c.folder)
                    add("edge", s.rel, inc.line,
                        "the relative include " + inc.path + " leaves " + c.name +
                            "; reach another component through its include root, never a relative path");
            }
        }
    }

    // Naming another component's namespace without depending on it: the coupling a transitive
    // include would otherwise hide.
    void check_namespaces(const Source& s) {
        const Component& c = *s.component;
        const std::vector<Token>& t = s.tokens;
        for (const Component& o : in_.rules.components) {
            if (&o == &c || o.cpp_namespace.empty() || c.depends.count(o.name)) continue;
            std::set<int> reported;
            for (std::size_t i = 0; i + 1 < t.size(); ++i) {
                if (t[i].kind != TokenKind::Identifier || t[i].text != o.cpp_namespace || t[i + 1].text != "::") continue;
                if (i > 0 && (t[i - 1].text == "." || t[i - 1].text == "->")) continue;
                const int line = line_of(s.stripped.code, t[i].offset);
                if (reported.insert(line).second)
                    add("edge", s.rel, line,
                        c.name + " names namespace " + o.cpp_namespace + " of " + o.name + ", which it does not depend on");
            }
        }
    }

    void check_consumer(const Source& s, const Component& dep) {
        const Component& c = *s.component;
        const std::vector<Token>& t = s.tokens;
        const std::string& ns = dep.cpp_namespace;
        auto api_list = in_.consumer_api.find(dep.name);
        const std::set<std::string> api = api_list == in_.consumer_api.end() ? std::set<std::string>{} : api_list->second.functions();
        const std::string list_name = dep.api_list;
        auto line = [&](std::size_t i) { return line_of(s.stripped.code, t[i].offset); };
        auto is = [&](std::size_t i, std::string_view text) { return i < t.size() && t[i].text == text; };
        auto skip_global = [&](std::size_t i) { return is(i, "::") ? i + 1 : i; };

        for (std::size_t i = 0; i < t.size(); ++i) {
            if (is(i, "using") && is(i + 1, "namespace") && is(skip_global(i + 2), ns))
                add("api-namespace", s.rel, line(i),
                    "using namespace " + ns + " hides which " + dep.name + " functions are called; write " + ns + ":: in full");
            if (is(i, "namespace") && !(i > 0 && is(i - 1, "using"))) {
                if (is(i + 1, ns) && (is(i + 2, "{") || is(i + 2, "::")))
                    add("api-namespace", s.rel, line(i), "reopening namespace " + ns + " adds to " + dep.name + " from outside it");
                if (i + 2 < t.size() && t[i + 1].kind == TokenKind::Identifier && is(i + 2, "=") && is(skip_global(i + 3), ns))
                    add("api-namespace", s.rel, line(i), "an alias for " + ns + " hides which " + dep.name + " functions are called");
            }
            if (t[i].kind == TokenKind::Identifier && t[i].text == ns && is(i + 1, "::") && i + 2 < t.size() &&
                t[i + 2].kind == TokenKind::Identifier) {
                if (i > 0 && (is(i - 1, ".") || is(i - 1, "->"))) continue;
                const std::string& name = t[i + 2].text;
                // snake_case is a function or a namespace by the repository's spelling; any other
                // name is still a function when the generator declares one by it.
                const bool function_like = lowercase_name(name) || internal_functions(dep).count(name) > 0;
                if (function_like && !is_keyword(name) && !api.count(name))
                    add("api-function", s.rel, line(i),
                        ns + "::" + name + " is not in " + dep.name + "'s public API (" + list_name +
                            "); call a listed function, or list it in a " + dep.name + " commit of its own first");
            }
        }

        const std::set<std::string>& internal = internal_functions(dep);
        const std::set<std::string>& own = self_declared_[c.name];
        for (const Call& call : unqualified_calls(t)) {
            if (!internal.count(call.name) || api.count(call.name) || own.count(call.name) || c.adl_ok.count(call.name))
                continue;
            add("api-adl", s.rel, line_of(s.stripped.code, call.offset),
                "unqualified call to " + call.name + ", which " + dep.name +
                    " declares but does not publish: argument-dependent lookup would reach it. Call a listed " + ns +
                    ":: function, rename yours, or clear the name with adl-ok in layers.txt");
        }
    }

    void check_vocabulary(const Source& s) {
        const Component& c = *s.component;
        std::set<std::pair<int, std::string>> reported;
        for (const Token& tok : s.tokens) {
            if (tok.kind != TokenKind::Identifier || is_keyword(tok.text)) continue;
            for (const std::string& word : words_of(tok.text)) {
                if (!c.deny_words.count(word)) continue;
                const int line = line_of(s.stripped.code, tok.offset);
                if (reported.insert({line, tok.text}).second)
                    add("vocab", s.rel, line,
                        tok.text + ": '" + word + "' is another layer's vocabulary and layers.txt keeps it out of " + c.name);
                break;
            }
        }
    }

    void check_cmake(const Component& c, const std::string& rel) {
        std::string text;
        try {
            text = read_file(in_.root / native(rel));
        } catch (const std::exception& e) {
            add("walk", rel, 0, e.what());
            return;
        }
        const std::string code = strip_cmake(text);
        // Tokens: split on whitespace, parentheses and quotes.
        struct Word {
            std::string text;
            std::size_t offset;
        };
        std::vector<Word> words;
        for (std::size_t i = 0; i < code.size();) {
            auto delimiter = [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')' || ch == '"'; };
            if (delimiter(code[i])) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < code.size() && !delimiter(code[j])) ++j;
            words.push_back({code.substr(i, j - i), i});
            i = j;
        }
        for (const Component& o : in_.rules.components) {
            if (&o == &c) continue;
            const bool allowed = c.depends.count(o.name) > 0;
            if (!allowed) {
                for (const std::string& needle : {"../" + o.folder, "..\\" + o.folder, "SOURCE_DIR}/" + o.folder}) {
                    for (std::size_t at = code.find(needle); at != std::string::npos; at = code.find(needle, at + 1)) {
                        const std::size_t after = at + needle.size();
                        const char next = after < code.size() ? code[after] : '\n';
                        if (next == '/' || next == '\\' || next == ')' || next == '"' || std::isspace(static_cast<unsigned char>(next)))
                            add("edge", rel, line_of(code, at),
                                c.name + "'s build reaches into " + o.folder + "/, and " + c.name + " does not depend on " + o.name);
                    }
                }
                for (const Word& w : words)
                    for (const std::string& target : o.targets)
                        if (w.text == target)
                            add("edge", rel, line_of(code, w.offset),
                                c.name + " links " + target + " of " + o.name + ", which it does not depend on");
            }
        }
        if (c.kind == Kind::Runtime)
            for (const Word& w : words)
                if (in_.rules.runtime_deny_links.count(lower(w.text)))
                    add("http", rel, line_of(code, w.offset),
                        "links " + w.text + ": runtime components ship fully local and link no network library");
        // A build fetches only through the manifest's helper, and only what the manifest names.
        if (!in_.rules.dependency_manifest.empty()) {
            const std::string& manifest = in_.rules.dependency_manifest;
            for (std::size_t i = 0; i < words.size(); ++i) {
                const std::string command = lower(words[i].text);
                const bool fetches = command == "fetchcontent_declare" || command == "fetchcontent_populate" ||
                                     command == "externalproject_add" ||
                                     (command == "file" && i + 1 < words.size() && words[i + 1].text == "DOWNLOAD");
                if (fetches)
                    add("deps", rel, line_of(code, words[i].offset),
                        words[i].text + " fetches outside " + manifest + ": name the file there (the owner approves each) and fetch it with " +
                            in_.rules.dependency_helper + "'s fwgc_dependency()");
                if (command == "fwgc_dependency" && i + 1 < words.size() && !build_dependencies_.count(words[i + 1].text))
                    add("deps", rel, line_of(code, words[i].offset),
                        "fwgc_dependency(" + words[i + 1].text + "): " + manifest + " names no build dependency by that name");
            }
        }
    }

    void check_path_text(const Component& c, const std::string& rel) {
        std::string text;
        try {
            text = read_file(in_.root / native(rel));
        } catch (const std::exception& e) {
            add("walk", rel, 0, e.what());
            return;
        }
        int number = 0;
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            ++number;
            const std::string line = text.substr(start, end - start);
            const std::size_t first = line.find_first_not_of(" \t");
            const std::string_view trimmed = first == std::string::npos ? std::string_view{} : std::string_view(line).substr(first);
            const bool comment = starts_with(trimmed, "//") || starts_with(trimmed, "/*") || starts_with(trimmed, "*") ||
                                 starts_with(trimmed, "<!--");
            if (!comment)
                for (const std::string& needle : c.deny_path_text)
                    if (line.find(needle) != std::string::npos)
                        add("path-text", rel, number,
                            c.name + " reaches into " + needle + "; it reads the generator through fwg.exe and the world document only");
            if (end == text.size()) break;
            start = end + 1;
        }
    }
};

}  // namespace

std::vector<Finding> check_tree(const Inputs& in) { return Checker(in).run(); }

}  // namespace layers
