// The layer checks: every rule in layers.txt, run over one tree.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "rules.hpp"

namespace layers {

struct Finding {
    std::string rule;  // edge, api-include, api-function, api-namespace, api-adl, api-list,
                       // vocab, lang, no-code, http, path-text, top-level
    std::string path;  // relative to the root, '/'-separated
    int line = 0;      // 0: the finding is about the path, not a line in it
    std::string message;
};

bool operator<(const Finding& a, const Finding& b);
bool operator==(const Finding& a, const Finding& b);

struct Inputs {
    std::filesystem::path root;
    Rules rules;
    // Per component with an api-list. consumer_api is what a sibling may call; own_api is
    // checked against the component's own headers. The commit gate reads consumer_api from
    // the last commit and own_api from the staged tree; a plain run reads both from `root`.
    std::map<std::string, ApiList> consumer_api;
    std::map<std::string, ApiList> own_api;
    // A working tree holds build output, so a plain run skips the ignore-dir names. A staged
    // snapshot holds only what is being committed, so the gate checks every file in it: a
    // source file force-added under a folder named build/ is still a source file.
    bool skip_ignored_dirs = true;
};

// Every finding, sorted. Unreadable files are findings too, never skipped silently.
std::vector<Finding> check_tree(const Inputs& in);

// Reads each component's api-list from `root`. A missing list is left out of the map (and
// check_tree reports it).
std::map<std::string, ApiList> load_api_lists(const Rules& rules, const std::filesystem::path& root);

std::string format(const Finding& f);

// Whole file as bytes; throws std::runtime_error when it cannot be read.
std::string read_file(const std::filesystem::path& path);

}  // namespace layers
