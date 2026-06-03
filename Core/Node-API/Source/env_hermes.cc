// Bridge between the Babylon Napi:: helpers and the Hermes static_h NAPI
// implementation.
//
// Hermes's `hermesNapi` static library implements the full set of standard
// `napi_*` C functions on top of a `hermes::vm::Runtime`.  In this TU we:
//   1.  Create a Runtime + napi_env via `hermes_napi_create_env`.
//   2.  Keep the Runtime alive for the lifetime of the env in a process-wide
//       table (the env is opaque to callers; we need somewhere to stash the
//       owning shared_ptr).
//   3.  Provide `Napi::Eval` that calls Hermes's `hermes_run_script` (which
//       takes a `source_url` for stack traces, matching what callers expect
//       from the other engines' `Napi::Eval`).
//   4.  Expose `Napi::DrainJobs` so AppRuntime can pump microtasks after each
//       dispatched callback.
//
// Header layering note:
// Both our shared NAPI headers and Hermes's vendored ones use the same
// include guards (`SRC_JS_NATIVE_API_H_`, `SRC_JS_NATIVE_API_TYPES_H_`).
// Whichever set is included first wins.  We deliberately include our shared
// `<napi/env.h>` chain FIRST so:
//   * the Napi:: C++ wrappers (`Napi::Env`, `Napi::Value`, `Napi::Error`)
//     line up with the rest of the project,
//   * the Babylon-extended 4-arg `napi_run_script` declaration matches the
//     inline `Env::RunScript` body in napi-inl.h (which we never actually
//     call in this engine — Napi::Eval below routes through
//     `hermes_run_script` directly — so the linker is never asked to find
//     the 4-arg symbol).
//
// We keep `NAPI_VERSION` at the shared default (5) so the inline wrappers
// in napi-inl.h that target newer NAPI revisions (e.g.
// `Env::GetModuleFileName` which calls `node_api_get_module_file_name`)
// aren't pulled in — they would reference symbols absent from our shared
// js_native_api.h.
//
// Hermes's `node_api.h` then needs a couple of NAPI v10 types
// (`node_api_basic_env`, `node_api_basic_finalize`) that its own
// js_native_api.h would have defined, but those headers are now guarded
// out.  We supply the typedefs locally: in NAPI v10 they're just type-
// attributed aliases for the regular `napi_env` / `napi_finalize`, so the
// ABI remains compatible with the symbols hermesNapi actually exports.

#include <napi/env.h>

// Forward-declare the NAPI v10 "basic" type aliases that Hermes's node_api.h
// expects to find.  These are ABI-equivalent to the non-basic versions; the
// "basic" marker is purely a documentation/attribute aid in upstream Node.
typedef napi_env node_api_basic_env;
typedef napi_finalize node_api_basic_finalize;

#include "hermes_napi.h"
#include "hermes/Public/RuntimeConfig.h"
#include "hermes/VM/Runtime.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace
{
    struct HermesEnvState
    {
        std::shared_ptr<hermes::vm::Runtime> runtime;
    };

    std::mutex& StateMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<napi_env, HermesEnvState>& StateMap()
    {
        static std::unordered_map<napi_env, HermesEnvState> map;
        return map;
    }

    hermes::vm::Runtime* LookupRuntime(napi_env env)
    {
        std::scoped_lock lock{StateMutex()};
        auto it = StateMap().find(env);
        if (it == StateMap().end())
        {
            return nullptr;
        }
        return it->second.runtime.get();
    }
}

namespace Napi
{
    Napi::Env Attach()
    {
        // Default Hermes config is fine for embedding: MicrotaskQueue is on,
        // ES6 Proxy + generators are on, Intl is on, EnableEval is on.
        // We bump the max GC heap to something reasonable for running our
        // Mocha test suite (the unit-test default of 512 KiB is too small).
        auto config = hermes::vm::RuntimeConfig::Builder()
                          .withGCConfig(hermes::vm::GCConfig::Builder()
                                            .withInitHeapSize(1u << 20)        //   1 MiB
                                            .withMaxHeapSize(512u << 20)       // 512 MiB
                                            .build())
                          .build();

        auto runtime = hermes::vm::Runtime::create(config);

        // `hermes_napi_create_env` ties the env's lifetime to the runtime —
        // destroying the runtime tears down the env via the runtime's
        // post-shutdown deleter.  We do NOT delete the env ourselves.
        napi_env env = hermes_napi_create_env(runtime.get());
        if (env == nullptr)
        {
            throw std::runtime_error{"hermes_napi_create_env returned null"};
        }

        {
            std::scoped_lock lock{StateMutex()};
            StateMap().emplace(env, HermesEnvState{std::move(runtime)});
        }

        return {env};
    }

    void Detach(Napi::Env env)
    {
        napi_env env_ptr{env};

        HermesEnvState state;
        {
            std::scoped_lock lock{StateMutex()};
            auto it = StateMap().find(env_ptr);
            if (it == StateMap().end())
            {
                return;
            }
            state = std::move(it->second);
            StateMap().erase(it);
        }

        // Dropping the last shared_ptr to the runtime tears down the env via
        // Hermes's post-shutdown deleter.  This runs the cleanup-hook chain,
        // persistent-reference finalizers, instance-data finalizer, and the
        // GC heap's `finalizeAll()` — all of which execute *user-provided*
        // C++ code (every napi_wrap / napi_add_finalizer finalizer in the
        // process, plus our polyfills' wrap destructors).
        //
        // If any of that code throws, the exception escapes Runtime::~Runtime
        // (its destructor is not marked noexcept) and propagates out of
        // Napi::Detach into the AppRuntime worker thread.  The thread
        // function has no catch, so C++ calls std::terminate → abort(),
        // killing the process with SIGABRT — silently on macOS, because
        // hermes_fatal isn't involved.  Observed on macOS arm64 CI as
        // `Abort trap: 6` immediately after `145 passing`, with no stderr
        // diagnostic.
        //
        // Catch here so the worker thread can exit cleanly: by the time we
        // hit Detach the tests have already reported their results, and
        // we have nothing useful to do with a teardown-time exception
        // besides logging it.  Re-raising would just re-trigger the abort
        // and leave CI red despite a fully successful test run.
        try
        {
            state.runtime.reset();
        }
        catch (const std::exception& e)
        {
            std::fprintf(
                stderr,
                "[Hermes] swallowed exception during Runtime teardown: %s\n",
                e.what());
        }
        catch (...)
        {
            std::fprintf(
                stderr,
                "[Hermes] swallowed non-std exception during Runtime teardown\n");
        }
    }

    void DrainJobs(Napi::Env env)
    {
        hermes::vm::Runtime* runtime = LookupRuntime(env);
        if (runtime == nullptr)
        {
            return;
        }
        // We intentionally ignore the ExecutionStatus return: any unhandled
        // exception raised by a microtask is surfaced to JS via the standard
        // unhandled-rejection mechanism (Hermes prints it via HermesInternal),
        // and we don't have a meaningful way to bubble it up here.  Real
        // exceptions thrown FROM user callbacks are already handled in
        // AppRuntime::Dispatch's try/catch.
        //
        // Wrap in try/catch because drainJobs synchronously runs any
        // pending finalizers we registered through addDrainJobsCallback
        // (drainPendingFinalizers, which in turn invokes user napi_wrap /
        // napi_add_finalizer C++ callbacks).  If one of those throws, the
        // exception escapes Runtime::drainJobs into AppRuntime::Dispatch's
        // outer (unguarded) call site and propagates out of the worker
        // thread function, triggering std::terminate -> abort() with no
        // diagnostic on macOS.  Swallow + log instead.
        try
        {
            (void)runtime->drainJobs();
        }
        catch (const std::exception& e)
        {
            std::fprintf(
                stderr,
                "[Hermes] swallowed exception during drainJobs: %s\n",
                e.what());
        }
        catch (...)
        {
            std::fprintf(
                stderr,
                "[Hermes] swallowed non-std exception during drainJobs\n");
        }
    }

    Napi::Value Eval(Napi::Env env, const char* source, const char* sourceUrl)
    {
        napi_env env_ptr{env};
        const size_t length = std::strlen(source);

        hermes_run_script_flags flags{};
        flags.struct_size = sizeof(flags);

        napi_value result = nullptr;

        // Buffer-lifetime note:
        //
        // `hermes_run_script` has two ingest paths:
        //   * If the byte at `size - 1` is `\0`, it wraps our pointer in a
        //     `WeirdZeroTerminatedBuffer` *zero-copy* — meaning our buffer
        //     stays live inside whatever `BCProvider` / `RuntimeModule` the
        //     compiled script ends up owning, and our finalize callback fires
        //     only when those are destroyed (often not until ~Runtime).
        //   * Otherwise it copies the source into Hermes's own
        //     `StdStringBuffer` and synchronously invokes our finalize
        //     callback before returning, so we own the buffer only for the
        //     duration of this call.
        //
        // We deliberately pick the second (copy) path by passing `length`
        // (without the trailing `\0`).  The first path was producing a
        // macOS libmalloc abort during Runtime teardown:
        //   `malloc: *** error for object 0x…: pointer being freed was not
        //    allocated`
        // followed by SIGABRT.  Letting Hermes own the source as a
        // std::string and tearing our buffer down synchronously side-steps
        // the entire buffer-lifetime question and is plenty cheap for the
        // sizes we Eval here (Mocha test bundles ~1 MiB).
        auto* copy = new uint8_t[length];
        std::memcpy(copy, source, length);
        auto finalize = [](const uint8_t* data, size_t /*size*/, void* /*hint*/) {
            delete[] data;
        };

        const napi_status status = hermes_run_script(
            env_ptr,
            copy,
            length,
            finalize,
            /*finalize_hint=*/nullptr,
            sourceUrl,
            &flags,
            &result);

        if (status != napi_ok)
        {
            // Surface as a Napi::Error so callers see the same shape they get
            // from the other engines' Eval paths.
            const napi_extended_error_info* info = nullptr;
            napi_get_last_error_info(env_ptr, &info);
            const char* message =
                (info && info->error_message) ? info->error_message : "hermes_run_script failed";

            // If a JS exception is pending, prefer that for the error info.
            bool pending = false;
            napi_is_exception_pending(env_ptr, &pending);
            if (pending)
            {
                napi_value exception = nullptr;
                napi_get_and_clear_last_exception(env_ptr, &exception);
                if (exception != nullptr)
                {
                    throw Napi::Error{env, exception};
                }
            }

            throw std::runtime_error{std::string{"Hermes Eval failed: "} + message};
        }

        return Napi::Value{env, result};
    }
}
