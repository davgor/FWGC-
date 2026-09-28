// The layer rules (tools/check_layers/layers.txt) and a component's public function list
// (generator/api.txt), parsed. The file formats are documented in the files themselves.
#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace layers {

enum class Kind { Runtime, Devtool, Assets };

struct Component {
    std::string name;
    std::string folder;  // top-level folder, relative to the repository root
    Kind kind = Kind::Runtime;

    std::vector<std::string> include_roots;    // "fwg/": an include with this prefix is ours
    std::vector<std::string> public_includes;  // "fwg/api/": what another component may include
    std::string include_dir;                   // where include_roots resolve, e.g. generator/include
    std::vector<std::string> targets;          // CMake targets another component would link
    std::set<std::string> depends;             // components this one may include, link and name
    std::string cpp_namespace;                 // "fwg"
    std::string api_list;                      // "generator/api.txt"
    std::string api_headers;                   // "generator/include/fwg/api"

    std::set<std::string> adl_ok;          // unqualified names cleared for this component
    std::set<std::string> deny_words;      // identifier words that do not belong here
    bool cpp_only = false;                 // runtime: no other language's source
    std::vector<std::string> cpp_only_except;
    bool no_code = false;                  // assets: no source at all
    std::vector<std::string> deny_path_text;
};

struct Rules {
    std::vector<Component> components;
    std::set<std::string> ignore_top;   // top-level entries that are not components (.git, ...)
    std::set<std::string> ignore_dirs;  // directory names skipped at any depth (build, ...)
    std::vector<std::string> skip_paths;
    std::set<std::string> forbid_top;   // names no top-level folder may take (common, utils, ...)
    std::set<std::string> non_cpp_ext;  // ".py", ... (lower case, with the dot)
    std::vector<std::string> runtime_deny_includes;
    std::set<std::string> runtime_deny_links;  // lower case
    // dependency-manifest <manifest> <helper>: everything fetched from outside is named in the
    // manifest (JSON, at the root), and a build fetches only through the helper's
    // fwgc_dependency(). Empty when layers.txt has no such line.
    std::string dependency_manifest;
    std::string dependency_helper;

    const Component* find(std::string_view name) const;
    const Component* by_folder(std::string_view folder) const;
};

class RulesError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Throws RulesError naming the source and line of the first problem.
Rules parse_rules(std::string_view text, const std::string& source);

struct ApiEntry {
    std::string function;
    std::vector<std::string> headers;
    int line = 0;
};

struct ApiList {
    std::string source;  // for messages
    std::vector<ApiEntry> entries;
    std::set<std::string> functions() const;
};

ApiList parse_api_list(std::string_view text, const std::string& source);

// Splits on whitespace; "a quoted word" stays one word. A '#' outside quotes ends the line.
std::vector<std::string> split_words(std::string_view line);

}  // namespace layers
