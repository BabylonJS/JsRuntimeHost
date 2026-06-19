#pragma once

#include "Dispatchable.h"

#include <Babylon/JsRuntime.h>

#include <napi/utilities.h>

#include <memory>
#include <functional>
#include <exception>

namespace Babylon
{
    class AppRuntime final
    {
    public:
        class Options
        {
        public:
            // Optional handler for unhandled exceptions.
            std::function<void(const Napi::Error&)> UnhandledExceptionHandler{DefaultUnhandledExceptionHandler};

            // Defines whether to enable the debugger. Only implemented for V8 and Chakra.
            bool EnableDebugger{false};

            // Waits for the debugger to be attached before the execution of any script. Only implemented for V8.
            bool WaitForDebugger{false};
        };

        AppRuntime();
        AppRuntime(Options options);
        ~AppRuntime();

        // Copy semantics
        AppRuntime(const AppRuntime&) = delete;
        AppRuntime& operator=(const AppRuntime&) = delete;

        // Move semantics
        AppRuntime(AppRuntime&&) = delete;
        AppRuntime& operator=(AppRuntime&&) = delete;

        void Suspend();
        void Resume();

        void Dispatch(Dispatchable<void(Napi::Env)> callback);

        // Routes an unhandled promise rejection to the embedder's UnhandledExceptionHandler (which
        // defaults to a benign logger), so an embedder's crash/telemetry pipeline can observe
        // fire-and-forget failures (e.g. an un-awaited fetch() that rejects) -- matching the browser
        // `unhandledrejection` behavior. Reporting is deferred to the end of the turn, so a rejection
        // handled synchronously (e.g. `const p = Promise.reject(e); p.catch(...)`) is not reported.
        //
        // Coverage is determined by whether the engine exposes a host promise-rejection hook:
        //   * V8 (Isolate::SetPromiseRejectCallback) -- supported on all platforms.
        //   * Apple JavaScriptCore (JSGlobalContextSetUnhandledRejectionCallback) -- supported. This
        //     is an SPI present only in Apple's JSC; the WebKitGTK JSC used on Linux does not expose
        //     it, so tracking is a no-op there.
        //   * Chakra (in-box/EdgeMode) and JSI -- no-op: neither exposes such a hook
        //     (JsSetHostPromiseRejectionTracker is ChakraCore-only, and neither jsi::Runtime nor
        //     V8JSI surfaces the V8 callback).
        //
        // Intended for internal (engine-implementation) use.
        void OnUnhandledPromiseRejection(const Napi::Error& error);

        // Default unhandled exception handler that outputs the error message to the program output.
        static void BABYLON_API DefaultUnhandledExceptionHandler(const Napi::Error& error);

    private:
        // These three methods are the mechanism by which platform- and JavaScript-specific
        // code can be "injected" into the execution of the JavaScript thread. These three
        // functions are implemented in separate files, thus allowing implementations to be
        // mixed and matched by the build system based on the platform and JavaScript engine
        // being targeted, without resorting to virtuality. An important nuance of these
        // functions is that they are all intended to call each other: RunPlatformTier MUST
        // call RunEnvironmentTier, which MUST create the initial Napi::Env and pass it to
        // Run. This arrangement allows not only for an arbitrary assemblage of platforms,
        // but it also allows us to respect the requirement by certain platforms (notably V8)
        // that certain program state be allocated and stored only on the stack.
        void RunPlatformTier();
        void RunEnvironmentTier(const char* executablePath = ".");
        void Run(Napi::Env);

        // This method is called from Dispatch to allow platform-specific code to add
        // extra logic around the invocation of a dispatched callback.
        void Execute(Dispatchable<void()> callback);

        Options m_options;

        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
