#include "fwg/workers.hpp"

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fwg {
namespace {

// What set_worker_threads stored, or -1 until it is called.
std::atomic<long long> g_requested{-1};

// FWG_THREADS, when it is set to anything. Empty counts as unset, which is what a POSIX
// shell's `FWG_THREADS= fwg ...` means.
long long environment_request() {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv: read here, never written by this library
#endif
    const char* raw = std::getenv("FWG_THREADS");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    if (raw == nullptr || *raw == '\0') return -1;
    const std::string_view text(raw);
    unsigned value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("FWG_THREADS must be a whole number of threads -- 0 for every hardware thread, "
                                    "1 for serial -- and is '" + std::string(text) + "'");
    }
    return value;
}

}  // namespace

void set_worker_threads(unsigned count) { g_requested.store(count); }

unsigned worker_threads() {
    long long requested = g_requested.load();
    if (requested < 0) requested = environment_request();
    if (requested > 0) return static_cast<unsigned>(requested);
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 1u : hardware;
}

void for_each_index(std::size_t count, const std::function<void(std::size_t)>& task) {
    const std::size_t threads = std::min<std::size_t>(worker_threads(), count);
    if (threads <= 1) {
        for (std::size_t i = 0; i < count; ++i) task(i);
        return;
    }

    // A thread starts in the DEFAULT floating-point environment, not its creator's, so a host
    // that set a rounding mode or flushes denormals would otherwise get one planner's
    // arithmetic in two environments -- and a different world on N threads than on one.
    std::fenv_t environment;
    std::fegetenv(&environment);

    // The work queue is the index range itself: each thread takes the next index until none is
    // left. Handing indices out in ascending order is what lets a failure stop the rest early.
    std::atomic<std::size_t> next{0};
    // The lowest index that has thrown so far, or `count` while none has. Every index below it
    // was handed out before it, so each of those runs to completion whatever happens here;
    // an index above it is skipped, as the serial loop would never have reached it.
    std::atomic<std::size_t> lowest_failure{count};
    std::vector<std::exception_ptr> failures(count);

    const auto work = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= count || i > lowest_failure.load()) return;
            try {
                task(i);
            } catch (...) {
                failures[i] = std::current_exception();
                std::size_t seen = lowest_failure.load();
                while (i < seen && !lowest_failure.compare_exchange_weak(seen, i)) {
                }
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (std::size_t t = 1; t < threads; ++t) {
        try {
            pool.emplace_back([&] {
                std::fesetenv(&environment);
                work();
            });
        } catch (...) {
            // A thread the system will not start is work the others do instead: fewer threads
            // is slower, never different.
            break;
        }
    }
    work();  // the calling thread is a worker too
    for (std::thread& thread : pool) thread.join();

    const std::size_t failed = lowest_failure.load();
    if (failed < count) std::rethrow_exception(failures[failed]);
}

}  // namespace fwg
