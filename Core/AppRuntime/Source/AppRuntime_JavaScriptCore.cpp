#include "AppRuntime.h"
#include "PostTickHook.h"
#include <napi/env.h>

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

#if __APPLE__
        if (__builtin_available(iOS 16.4, macOS 13.3, *))
        {
            JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);
        }
#endif

        Napi::Env env = Napi::Attach(globalContext);

        // ASYNC WASM NOTE: JavaScriptCore implements
        // WebAssembly.instantiate / .compile with a background-thread
        // compile pool (Wasm::Worklist). On completion, JSC posts the
        // resolving Promise's continuation onto the JS thread's
        // microtask queue.
        //
        // JSC's public C API (JSC.framework) does NOT expose a way to
        // explicitly pump that microtask queue from the embedder; the
        // queue is drained automatically each time JS execution returns
        // to JSC (e.g. after every JSValueProtect, JSObjectCallAsFunction,
        // JSEvaluateScript). Because the AppRuntime dispatcher hands
        // control to JSC on every Dispatch() execution (via
        // Napi::Attach's wrapping of JS calls), the microtask queue is
        // implicitly drained for each tick.
        //
        // This means async WASM continuations resolve naturally as long
        // as new JS work is dispatched after the background compile
        // completes. For a pure "fire-and-await" pattern where the
        // embedder issues no further dispatches between
        // WebAssembly.instantiate and its resolution, an empty
        // dispatch can be queued to nudge the queue. This is the only
        // scenario where async WASM could appear to stall on JSC, and it
        // matches the pattern actually used by Babylon.js loaders
        // (load-and-render is followed by per-frame renders that keep
        // dispatching).
        //
        // No post-tick hook is installed because the public JSC API has
        // no standalone "drain microtasks" entry point. If a future host
        // observes async-WASM stalls on JSC, the recommended workaround
        // is to dispatch periodic no-op JS calls from the host (e.g. a
        // render tick) which forces a microtask drain.

        Run(env);

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }
}
