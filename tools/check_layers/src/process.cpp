#include "process.hpp"

#ifndef _WIN32
#error "process.cpp is written for Windows only: port run() before building the commit gate elsewhere"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cwchar>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace layers::process {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

fs::path temp_file(const char* tag) {
    static std::atomic<unsigned> counter{0};
    return fs::temp_directory_path() / ("fwgc_gate_" + std::to_string(GetCurrentProcessId()) + "_" +
                                        std::to_string(counter++) + "_" + tag);
}

HANDLE open_inheritable(const fs::path& p, bool write) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    return CreateFileW(p.c_str(), write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool starts_with_ci(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

// This process's environment without the variables whose names start with any of `prefixes`.
std::wstring environment_without(const std::vector<std::string>& prefixes) {
    std::vector<std::wstring> wide;
    for (const std::string& p : prefixes) wide.push_back(widen(p));
    LPWCH block = GetEnvironmentStringsW();
    std::wstring out;
    for (LPWCH p = block; *p; p += std::wcslen(p) + 1) {
        const std::wstring entry(p);
        const std::size_t eq = entry.find(L'=', 1);  // "=C:=C:\..." entries start with '='
        const std::wstring name = entry.substr(0, eq);
        bool drop = false;
        for (const std::wstring& w : wide)
            if (starts_with_ci(name, w)) drop = true;
        if (!drop) {
            out += entry;
            out.push_back(L'\0');
        }
    }
    FreeEnvironmentStringsW(block);
    out.push_back(L'\0');
    return out;
}

}  // namespace

std::optional<std::string> env(const char* name) {
    const std::wstring w = widen(name);
    DWORD n = GetEnvironmentVariableW(w.c_str(), nullptr, 0);
    if (n == 0) return std::nullopt;
    std::wstring value(n, L'\0');
    n = GetEnvironmentVariableW(w.c_str(), value.data(), n);
    value.resize(n);
    return narrow(value);
}

std::string quote_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back('"');
    return out;
}

std::optional<fs::path> find_on_path(const std::string& name) {
    const std::optional<std::string> path = env("PATH");
    if (!path) return std::nullopt;
    std::stringstream entries(*path);
    std::string dir;
    while (std::getline(entries, dir, ';')) {
        if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') dir = dir.substr(1, dir.size() - 2);
        if (dir.empty()) continue;
        for (const char* ext : {".exe", ""}) {
            const fs::path candidate = fs::path(widen(dir)) / widen(name + ext);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return candidate;
        }
    }
    return std::nullopt;
}

Result run(const std::vector<std::string>& argv, const std::string& input, const fs::path& cwd, unsigned timeout_ms,
           const std::vector<std::string>& unset) {
    if (argv.empty()) throw std::runtime_error("run: no program");
    fs::path program(widen(argv[0]));
    if (!program.has_parent_path()) {
        const std::optional<fs::path> found = find_on_path(argv[0]);
        if (!found) throw std::runtime_error("cannot find " + argv[0] + " on PATH");
        program = *found;
    }
    std::string command = quote_arg(narrow(program.wstring()));
    for (std::size_t k = 1; k < argv.size(); ++k) command += " " + quote_arg(argv[k]);
    std::wstring wcommand = widen(command);

    const fs::path in_path = temp_file("in"), out_path = temp_file("out"), err_path = temp_file("err");
    {
        std::ofstream f(in_path, std::ios::binary);
        f << input;
    }
    HANDLE hin = open_inheritable(in_path, false);
    HANDLE hout = open_inheritable(out_path, true);
    HANDLE herr = open_inheritable(err_path, true);
    auto remove_files = [&] {
        std::error_code ec;
        fs::remove(in_path, ec);
        fs::remove(out_path, ec);
        fs::remove(err_path, ec);
    };
    auto close_files = [&] {
        for (HANDLE h : {hin, hout, herr})
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    };
    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE || herr == INVALID_HANDLE_VALUE) {
        close_files();
        remove_files();
        throw std::runtime_error("cannot create the temporary files for " + argv[0]);
    }

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hin;
    si.hStdOutput = hout;
    si.hStdError = herr;
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    std::wstring environment;
    LPVOID environment_block = nullptr;
    if (!unset.empty()) {
        environment = environment_without(unset);
        environment_block = environment.data();
        flags |= CREATE_UNICODE_ENVIRONMENT;
    }
    const std::wstring wcwd = cwd.empty() ? std::wstring{} : cwd.wstring();
    const BOOL started = CreateProcessW(program.c_str(), wcommand.data(), nullptr, nullptr, TRUE, flags, environment_block,
                                        wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    close_files();
    if (!started) {
        const DWORD error = GetLastError();
        remove_files();
        throw std::runtime_error("cannot start " + argv[0] + " (Windows error " + std::to_string(error) + ")");
    }

    // A job holds the child and anything it starts, so a timeout ends the whole tree.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(pi.hThread);

    Result r;
    const DWORD waited = WaitForSingleObject(pi.hProcess, timeout_ms ? timeout_ms : INFINITE);
    if (waited == WAIT_TIMEOUT) {
        if (job) TerminateJobObject(job, 124);
        else TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 10000);
        r.timed_out = true;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);  // ends anything the child left running
    r.out = slurp(out_path);
    r.err = slurp(err_path);
    remove_files();
    return r;
}

}  // namespace layers::process
