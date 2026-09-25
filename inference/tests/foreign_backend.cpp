// A backend the runtime does not ship, for the test that the runtime unloads it. It stands for
// ggml's RPC backend, which talks over sockets, or anything else GGML_BACKEND_PATH could name.
// It has no devices. Loaded, it leaves a mark in the environment, so the test can see it was
// offered.
#include "ggml-backend-impl.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

const char* name(ggml_backend_reg_t) { return "Foreign"; }
size_t device_count(ggml_backend_reg_t) { return 0; }
ggml_backend_dev_t device(ggml_backend_reg_t, size_t) { return nullptr; }
void* proc_address(ggml_backend_reg_t, const char*) { return nullptr; }

ggml_backend_reg foreign_reg = {GGML_BACKEND_API_VERSION, {name, device_count, device, proc_address}, nullptr};

ggml_backend_reg_t foreign() {
#ifdef _WIN32
    SetEnvironmentVariableA("INFERENCE_FOREIGN_BACKEND_LOADED", "1");
#endif
    return &foreign_reg;
}

}  // namespace

GGML_BACKEND_DL_IMPL(foreign)
