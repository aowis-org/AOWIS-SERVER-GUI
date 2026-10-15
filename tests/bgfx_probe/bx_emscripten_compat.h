// Compiler compatibility includes for bx's Emscripten POSIX implementation.
// Scoped to the bx target: never injected into Qt or the AOWIS application.
#pragma once
#if !defined(__EMSCRIPTEN__)
#error "bx_emscripten_compat.h is only for Emscripten builds"
#endif
#include "emscripten_version_compat.h"

#include <time.h>     // timespec, time_t, nanosleep
#include <sched.h>    // sched_yield
#include <dlfcn.h>    // dlopen, dlclose, dlsym, RTLD_*
#include <pthread.h>  // pthread_mutex_t and related declarations
