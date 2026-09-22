#pragma once

#include <napi/napi.h>

namespace Napi
{
    // Create a Hermes runtime + napi_env owned by this process and expose it
    // through Napi::Env.  The runtime lives until the matching Detach() call.
    Napi::Env Attach();

    // Tear down the runtime that backs `env`.  After this call, `env` is
    // invalid.  Hermes owns the env lifetime via its Runtime; destroying the
    // runtime destroys the env, so we explicitly route Detach through a
    // reverse lookup that drops the owning std::shared_ptr<vm::Runtime>.
    void Detach(Napi::Env env);

    // Compile and execute UTF-8 source on the current Hermes runtime.
    // `sourceUrl` is attached to stack traces. This calls Hermes's
    // `hermes_run_script` with an owned source copy; `Env::RunScript` works
    // too, through the source-URL overload of `napi_run_script` that
    // env_hermes.cc defines.
    Napi::Value Eval(Napi::Env env, const char* source, const char* sourceUrl);

    // Pump Hermes's job queue (drains microtasks and pending finalizers).
    // The application runtime must call this once per dispatched callback
    // so that Promise continuations, queueMicrotask, and other deferred
    // work scheduled during the callback actually runs before the next
    // top-level dispatch.  Equivalent engines (V8, Chakra) auto-drain
    // microtasks at scope exit; Hermes requires an explicit drainJobs().
    void DrainJobs(Napi::Env env);
}
