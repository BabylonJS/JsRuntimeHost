#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

#include <string>

namespace Babylon::Polyfills::Console
{
    /**
     * Importance level of messages sent via logging callbacks.
     */
    enum class LogLevel
    {
        Log,
        Warn,
        Error,
    };

    using CallbackT = std::function<void BABYLON_API (const char*, LogLevel)>;

    void BABYLON_API Initialize(Napi::Env env, CallbackT callback);

    /**
     * Capture the JavaScript callstack at the current JS execution point.
     *
     * Only meaningful when called from within a `CallbackT` invocation (or any other Napi callback
     * the polyfill itself registered) -- captures the JS frames that produced the originating
     * `console.*` call. The polyfill's own shim frame(s) are skipped, so the top frame is the
     * user's call site.
     *
     * Format is engine-defined opaque text -- treat as diagnostic-only, do not parse. Returns an
     * empty string if no JS context is active or if stack capture fails for any reason.
     *
     * Cost is non-trivial (currently implemented via `new Error().stack` under the hood); the
     * caller decides per-message whether to invoke. Hosts that want stacks only on error
     * messages can branch on `LogLevel`; hosts that want them everywhere are free to do so;
     * hosts that don't care pay nothing.
     */
    std::string BABYLON_API CaptureCurrentJsStack(Napi::Env env);
}
