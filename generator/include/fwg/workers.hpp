// How many threads the library may run on, and the one loop that runs on them.
//
// The count is a PROCESS setting, and deliberately not a field of Config or Request: both are
// serialised into the world document, and a thread count there would make two documents of
// one world differ by the machine that built them. Nothing may depend on it but the wall
// clock. tests/test_workers.cpp generates the same worlds serially and on 2, 3 and every
// hardware thread and compares the documents byte for byte; docs/determinism.md section 4
// says what runs in parallel and why its output cannot depend on the count.
#pragma once

#include <cstddef>
#include <functional>

namespace fwg {

// 0 means every hardware thread; 1 means serial, and then no thread is ever started. Any
// other value is the most threads one parallel pass runs on, the calling thread included.
//
// Until this is called, the FWG_THREADS environment variable supplies the count with the same
// meaning, and without it the count is the hardware concurrency.
void set_worker_threads(unsigned count);

// The count a parallel pass started now would use. Never 0: an unknown hardware concurrency
// counts as 1.
//
// Throws std::invalid_argument when FWG_THREADS supplies the count and is not a whole number.
// A malformed value is refused rather than read as a default: it was set on purpose.
unsigned worker_threads();

// Runs task(0) .. task(count - 1) on up to worker_threads() threads, the calling thread among
// them, and returns once every task that started has finished. With one worker, or one task,
// it is the plain loop `for (i = 0; i < count; ++i) task(i)` on the calling thread.
//
// What keeps the result independent of the thread count, and the caller's side of it:
//
//   * A task writes only what its own index owns -- slot i of a vector sized before the call
//     -- and reads nothing another task writes. Results are then in index order whatever
//     order the threads finished in.
//   * If tasks throw, the exception rethrown is the one thrown by the LOWEST index that threw,
//     unchanged: the one the serial loop would have thrown, since every index below it ran and
//     returned. Indices above a failed one may be skipped.
//   * Every task runs in the calling thread's floating-point environment -- rounding mode and,
//     on MSVC, denormal handling -- which a new thread would otherwise not inherit.
void for_each_index(std::size_t count, const std::function<void(std::size_t)>& task);

}  // namespace fwg
