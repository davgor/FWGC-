#include "rules.hpp"

#include <algorithm>
#include <cctype>

namespace layers {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    return out;
}

}  // namespace

const Component* Rules::find(std::string_view name) const {
    for (const Component& c : components)
        if (c.name == name) return &c;
    return nullptr;
}

const Component* Rules::by_folder(std::string_view folder) const {
    for (const Component& c : components)
        if (c.folder == folder) return &c;
    return nullptr;
}

std::set<std::string> ApiList::functions() const {
    std::set<std::string> out;
    for (const ApiEntry& e : entries) out.insert(e.function);
    return out;
}

std::vector<std::string> split_words(std::string_view line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '#') break;
        if (c == '"') {
            const std::size_t end = line.find('"', i + 1);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end;
            out.emplace_back(line.substr(i + 1, stop - i - 1));
            i = stop + 1;
            continue;
        }
        std::size_t j = i;
        while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j])) && line[j] != '#') ++j;
        out.emplace_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

Rules parse_rules(std::string_view text, const std::string& source) {
    Rules rules;
    int number = 0;
    for (std::string_view raw : lines_of(text)) {
        ++number;
        const std::vector<std::string> w = split_words(raw);
        if (w.empty()) continue;
        const std::string where = source + ":" + std::to_string(number) + ": ";
        auto need = [&](std::size_t count) {
            if (w.size() < count)
                throw RulesError(where + "'" + w[0] + "' needs at least " + std::to_string(count - 1) + " argument(s)");
        };
        auto component = [&](const std::string& name) -> Component& {
            for (Component& c : rules.components)
                if (c.name == name) return c;
            throw RulesError(where + "unknown component '" + name + "' (declare it with 'component' first)");
        };
        auto rest = [&](std::size_t from) { return std::vector<std::string>(w.begin() + static_cast<std::ptrdiff_t>(from), w.end()); };

        const std::string& d = w[0];
        if (d == "component") {
            need(4);
            if (w.size() != 4) throw RulesError(where + "component takes <name> <folder> <runtime|devtool|assets>");
            if (rules.find(w[1])) throw RulesError(where + "component '" + w[1] + "' declared twice");
            if (rules.by_folder(w[2])) throw RulesError(where + "folder '" + w[2] + "' belongs to two components");
            Component c;
            c.name = w[1];
            c.folder = w[2];
            if (w[3] == "runtime") c.kind = Kind::Runtime;
            else if (w[3] == "devtool") c.kind = Kind::Devtool;
            else if (w[3] == "assets") c.kind = Kind::Assets;
            else throw RulesError(where + "kind must be runtime, devtool or assets, not '" + w[3] + "'");
            rules.components.push_back(std::move(c));
        } else if (d == "include-root") {
            need(3);
            for (const std::string& p : rest(2)) component(w[1]).include_roots.push_back(p);
        } else if (d == "public-include") {
            need(3);
            for (const std::string& p : rest(2)) component(w[1]).public_includes.push_back(p);
        } else if (d == "include-dir") {
            need(3);
            component(w[1]).include_dir = w[2];
        } else if (d == "targets") {
            need(3);
            for (const std::string& t : rest(2)) component(w[1]).targets.push_back(t);
        } else if (d == "depends") {
            need(3);
            Component& c = component(w[1]);
            for (const std::string& dep : rest(2)) {
                component(dep);  // must exist
                if (dep == c.name) throw RulesError(where + c.name + " cannot depend on itself");
                c.depends.insert(dep);
            }
        } else if (d == "namespace") {
            need(3);
            component(w[1]).cpp_namespace = w[2];
        } else if (d == "api-list") {
            need(3);
            component(w[1]).api_list = w[2];
        } else if (d == "api-headers") {
            need(3);
            component(w[1]).api_headers = w[2];
        } else if (d == "adl-ok") {
            need(3);
            for (const std::string& n : rest(2)) component(w[1]).adl_ok.insert(n);
        } else if (d == "deny-word") {
            need(3);
            for (const std::string& n : rest(2)) component(w[1]).deny_words.insert(lower(n));
        } else if (d == "cpp-only") {
            need(2);
            Component& c = component(w[1]);
            c.cpp_only = true;
            if (w.size() > 2) {
                if (w[2] != "except" || w.size() < 4) throw RulesError(where + "cpp-only <component> [except <path>...]");
                for (const std::string& p : rest(3)) c.cpp_only_except.push_back(p);
            }
        } else if (d == "no-code") {
            need(2);
            component(w[1]).no_code = true;
        } else if (d == "deny-path-text") {
            need(3);
            for (const std::string& p : rest(2)) component(w[1]).deny_path_text.push_back(p);
        } else if (d == "ignore-top") {
            need(2);
            for (const std::string& p : rest(1)) rules.ignore_top.insert(p);
        } else if (d == "ignore-dir") {
            need(2);
            for (const std::string& p : rest(1)) rules.ignore_dirs.insert(p);
        } else if (d == "skip-path") {
            need(2);
            for (const std::string& p : rest(1)) rules.skip_paths.push_back(p);
        } else if (d == "forbid-top") {
            need(2);
            for (const std::string& p : rest(1)) rules.forbid_top.insert(p);
        } else if (d == "non-cpp-ext") {
            need(2);
            for (const std::string& p : rest(1)) {
                if (p.empty() || p[0] != '.') throw RulesError(where + "extensions start with '.': '" + p + "'");
                rules.non_cpp_ext.insert(lower(p));
            }
        } else if (d == "deny-include") {
            need(3);
            if (w[1] != "runtime") throw RulesError(where + "deny-include applies to 'runtime' only");
            for (const std::string& p : rest(2)) rules.runtime_deny_includes.push_back(lower(p));
        } else if (d == "deny-link") {
            need(3);
            if (w[1] != "runtime") throw RulesError(where + "deny-link applies to 'runtime' only");
            for (const std::string& p : rest(2)) rules.runtime_deny_links.insert(lower(p));
        } else {
            throw RulesError(where + "unknown directive '" + d + "'");
        }
    }
    for (const Component& c : rules.components) {
        if (!c.api_list.empty() && (c.api_headers.empty() || c.cpp_namespace.empty() || c.include_dir.empty()))
            throw RulesError(source + ": component '" + c.name +
                             "' has an api-list but no api-headers, namespace or include-dir");
        if (rules.forbid_top.count(c.folder))
            throw RulesError(source + ": component '" + c.name + "' uses the forbidden folder name '" + c.folder + "'");
    }
    if (rules.components.empty()) throw RulesError(source + ": declares no components");
    return rules;
}

ApiList parse_api_list(std::string_view text, const std::string& source) {
    ApiList list;
    list.source = source;
    int number = 0;
    std::set<std::string> seen;
    for (std::string_view raw : lines_of(text)) {
        ++number;
        const std::vector<std::string> w = split_words(raw);
        if (w.empty()) continue;
        if (w.size() < 2)
            throw RulesError(source + ":" + std::to_string(number) + ": '" + w[0] +
                             "' names no fwg/api header; write <function> <api-header>...");
        if (!seen.insert(w[0]).second)
            throw RulesError(source + ":" + std::to_string(number) + ": '" + w[0] + "' is listed twice");
        list.entries.push_back({w[0], std::vector<std::string>(w.begin() + 1, w.end()), number});
    }
    return list;
}

}  // namespace layers
