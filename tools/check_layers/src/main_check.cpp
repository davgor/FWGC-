// check_layers: the layer rules over one tree. See ../README.md.
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "check.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kUsage =
    "usage: check_layers [--root DIR] [--rules FILE]\n"
    "\n"
    "Checks the tree at DIR (default: the current directory) against FILE (default:\n"
    "DIR/tools/check_layers/layers.txt), reading each component's api-list from DIR.\n"
    "Prints one finding per line. Exit status: 0 clean, 1 findings, 2 unusable input.\n";

}  // namespace

int main(int argc, char** argv) {
    fs::path root = fs::current_path();
    fs::path rules_file;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--root" || arg == "--rules") && i + 1 < argc) {
            (arg == "--root" ? root : rules_file) = fs::path(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return 0;
        } else {
            std::cerr << "check_layers: unexpected argument '" << arg << "'\n" << kUsage;
            return 2;
        }
    }
    if (rules_file.empty()) rules_file = root / "tools" / "check_layers" / "layers.txt";
    try {
        layers::Inputs in;
        in.root = root;
        in.rules = layers::parse_rules(layers::read_file(rules_file), rules_file.generic_string());
        in.consumer_api = layers::load_api_lists(in.rules, root);
        in.own_api = in.consumer_api;
        const std::vector<layers::Finding> findings = layers::check_tree(in);
        for (const layers::Finding& f : findings) std::cout << layers::format(f) << '\n';
        std::cout << (findings.empty() ? "check_layers: clean\n"
                                       : "check_layers: " + std::to_string(findings.size()) + " finding(s)\n");
        return findings.empty() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "check_layers: " << e.what() << '\n';
        return 2;
    }
}
