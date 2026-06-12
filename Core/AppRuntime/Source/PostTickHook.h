#pragma once

#include <functional>

namespace Babylon::internal
{
    // Per-thread post-tick hook installed by engine-specific code (e.g.
    // the V8 environment tier) so it can drain engine-managed task
    // queues that are not part of the AppRuntime dispatcher.
    //
    // Motivating case: V8's foreground task runner holds the
    // continuations for asynchronous WebAssembly compilation
    // (WebAssembly.instantiate / .compile). Those tasks are scheduled
    // by V8's background worker threads when WASM compilation
    // completes and must be drained by the embedder via
    // v8::platform::PumpMessageLoop. Without that drain, any code that
    // awaits async WASM (Draco / Basis / KTX2 emscripten glue, etc.)
    // freezes forever because the resolving Promise's continuation is
    // never invoked.
    //
    // Defined in AppRuntime.cpp; the engine-agnostic dispatcher loop
    // calls the installed hook between ticks.
    void SetPostTickHook(std::function<void()> hook);
}
