// The C interface's one gate. Every fwgc_ entry point runs its work through enter(), so:
// - no exception crosses into the game: each becomes a status and a message for fwgc_last_error();
// - the world is made and moved on in one float environment, whatever the calling thread's is;
// - one process-wide lock covers the generator, whose caches are not all safe for two threads;
// - a handle that failed part way through a change refuses everything but its save.
//
// Header-only, so a test can drive it with a handle of its own.
#pragma once

#include <exception>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#include <float.h>
#else
#include <cfenv>
#endif

#include "orchestrator/director.hpp"
#include "orchestrator/fwgc_director.h"

namespace orchestrator::c_api {

// A caller's mistake the interface itself catches: a NULL, text that is not UTF-8, an unknown act.
struct BadArgument : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};
// A request the generator refused; what() is its refusal document where it has one.
struct Refused : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// A save that does not restore here.
struct Incompatible : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// This thread's last failure, for fwgc_last_error().
inline std::string& last_error() {
    thread_local std::string text;
    return text;
}

// The one lock every entry point holds.
inline std::mutex& world_lock() {
    static std::mutex lock;
    return lock;
}

// The float environment a world is made and moved on in -- round to nearest, denormals kept,
// every exception masked -- for the life of one call, and the caller's back after it. The
// generator's worker threads copy the calling thread's (generator/src/workers.cpp), so a host
// that flushes denormals would otherwise make a different world.
class FloatEnvironment {
public:
    FloatEnvironment() noexcept {
#if defined(_MSC_VER)
        unsigned int now = 0;
        _controlfp_s(&saved_, 0, 0);
        _controlfp_s(&now, _RC_NEAR | _DN_SAVE | _MCW_EM, kMask);
#else
        saved_ = std::fegetround();
        std::fesetround(FE_TONEAREST);
#endif
    }
    ~FloatEnvironment() {
#if defined(_MSC_VER)
        unsigned int now = 0;
        _controlfp_s(&now, saved_ & kMask, kMask);
#else
        std::fesetround(saved_);
#endif
    }
    FloatEnvironment(const FloatEnvironment&) = delete;
    FloatEnvironment& operator=(const FloatEnvironment&) = delete;

private:
#if defined(_MSC_VER)
    static constexpr unsigned int kMask = _MCW_RC | _MCW_DN | _MCW_EM;
    unsigned int saved_ = 0;
#else
    int saved_ = 0;
#endif
};

inline fwgc_status failed(fwgc_status status, std::string text) {
    last_error() = std::move(text);
    return status;
}

// What a call on no handle passes as its handle.
struct NoHandle {
    bool poisoned = false;
};
inline NoHandle* const kNoHandle = nullptr;

// Runs `work` (which returns an fwgc_status) for `handle` -- anything with a `bool poisoned`, or
// null for a call on no handle. `changes` says whether the work changes the handle: only then
// does a failure it did not see coming poison it. `saving` lets a poisoned handle through.
template <typename Handle, typename Work>
fwgc_status enter(Handle* handle, bool changes, bool saving, Work&& work) noexcept {
    try {
        last_error().clear();
        const FloatEnvironment floats;
        const std::lock_guard<std::mutex> locked(world_lock());
        if (handle != nullptr && handle->poisoned && !saving)
            return failed(FWGC_POISONED, "this director failed part way through a change: restore it from its save");
        const auto poison = [&] {
            if constexpr (!std::is_const_v<Handle>) {
                if (handle != nullptr && changes) handle->poisoned = true;
            }
        };
        try {
            return std::forward<Work>(work)();
        } catch (const Error& e) {  // the director's word for a caller's mistake: nothing changed
            return failed(FWGC_BAD_ARGUMENT, e.what());
        } catch (const BadArgument& e) {
            return failed(FWGC_BAD_ARGUMENT, e.what());
        } catch (const Refused& e) {
            return failed(FWGC_REFUSED, e.what());
        } catch (const Incompatible& e) {
            return failed(FWGC_INCOMPATIBLE, e.what());
        } catch (const std::bad_alloc&) {
            poison();
            return failed(FWGC_OUT_OF_MEMORY, changes ? "out of memory part way through a change: restore this director "
                                                        "from its save"
                                                      : "out of memory");
        } catch (const std::exception& e) {
            poison();
            return failed(FWGC_FAILED, e.what());
        } catch (...) {
            poison();
            return failed(FWGC_FAILED, "an unknown failure");
        }
    } catch (...) {
        return FWGC_OUT_OF_MEMORY;  // not even the message could be kept
    }
}

}  // namespace orchestrator::c_api
