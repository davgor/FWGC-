// Running a child process with its standard streams in files: git for the gate's plumbing,
// and the claude CLI for the review. Windows only for now (the repository's one platform);
// process.cpp refuses to compile elsewhere rather than pretend.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace layers::process {

struct Result {
    int exit_code = -1;
    bool timed_out = false;
    std::string out;
    std::string err;
};

// Runs argv (argv[0] resolved on PATH when it has no directory part), feeding `input` on
// standard input. timeout_ms 0 waits forever; a timed-out child is terminated with everything
// it started. The child inherits this environment less every variable whose name starts with
// one of `unset` (case-insensitive). Throws std::runtime_error when it cannot be started.
Result run(const std::vector<std::string>& argv, const std::string& input = {},
           const std::filesystem::path& cwd = {}, unsigned timeout_ms = 0,
           const std::vector<std::string>& unset = {});

// Windows command-line quoting of one argument, as CommandLineToArgvW reads it back.
std::string quote_arg(const std::string& arg);

// The first PATH entry holding `name` (with .exe tried too).
std::optional<std::filesystem::path> find_on_path(const std::string& name);

std::optional<std::string> env(const char* name);

}  // namespace layers::process
