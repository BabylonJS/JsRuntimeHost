#include "AppRuntime.h"
#include <napi/env.h>

#if __has_include(<JavaScriptCore/JSContextRefPrivate.h>)
#include <JavaScriptCore/JSContextRefPrivate.h>
#define JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT 1
#elif defined(__unix__) || defined(__APPLE__)
// Apple's SDK ships no JSContextRefPrivate.h either, but the system JavaScriptCore framework
// exports the same C ABI (it is SPI there); resolving it dynamically keeps Worker::terminate()
// able to stop a tight loop, exactly as on WebKitGTK. Without it a terminated worker that never
// yields keeps its thread forever and the finalizer's join deadlocks at teardown.
#include <dlfcn.h>
#define JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT 1
#endif

namespace
{
#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT) || \
    defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
    using ShouldTerminateCallback = bool (*)(JSContextRef, void*);
    using SetExecutionTimeLimit = void (*)(JSContextGroupRef, double, ShouldTerminateCallback, void*);
    using ClearExecutionTimeLimit = void (*)(JSContextGroupRef);

    // How often running script is polled for Terminate().
    constexpr double ExecutionTimeLimitSeconds{0.05};

    struct ExecutionWatchdog
    {
        Babylon::AppRuntime* Runtime{};
        SetExecutionTimeLimit Set{};
        ClearExecutionTimeLimit Clear{};
        ShouldTerminateCallback Callback{};

        bool Available() const
        {
            return Set != nullptr && Clear != nullptr;
        }

        // JavaScriptCore restarts the watchdog timer only when the callback sets a new limit
        // itself: its "callback did nothing" branch takes the CPU deadline that has just expired
        // for a timer the callback started and schedules nothing, so a single declined poll would
        // otherwise silence the watchdog until the running script returns on its own.
        void Rearm(JSContextRef context)
        {
            Set(JSContextGetGroup(context), ExecutionTimeLimitSeconds, Callback, this);
        }
    };
#endif
}

#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT) || \
    defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
        auto contextGroup = JSContextGetGroup(globalContext);
        ExecutionWatchdog watchdog{this};
#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT)
        watchdog.Set = &JSContextGroupSetExecutionTimeLimit;
        watchdog.Clear = &JSContextGroupClearExecutionTimeLimit;
#else
        // WebKitGTK deliberately omits JSContextRefPrivate.h from its dev
        // package, but current system builds export the same C ABI. Resolve it
        // dynamically so JsRuntimeHost stays buildable against the public
        // package and gracefully falls back to between-dispatch termination on
        // older builds that do not export the watchdog.
        watchdog.Set = reinterpret_cast<SetExecutionTimeLimit>(
            dlsym(RTLD_DEFAULT, "JSContextGroupSetExecutionTimeLimit"));
        watchdog.Clear = reinterpret_cast<ClearExecutionTimeLimit>(
            dlsym(RTLD_DEFAULT, "JSContextGroupClearExecutionTimeLimit"));
#endif
        // Poll at a modest interval while JS is running. Returning true from
        // this callback raises a catchable termination exception (a bare
        // string, "JavaScript execution terminated.") and lets the AppRuntime
        // thread unwind, so Worker::terminate() also stops a tight loop that
        // never reaches the dispatch queue.
        watchdog.Callback = [](JSContextRef context, void* data) {
            auto* watchdog = static_cast<ExecutionWatchdog*>(data);
            if (watchdog->Runtime->IsExecutionTerminationRequested())
            {
                return true;
            }
            watchdog->Rearm(context);
            return false;
        };
        if (watchdog.Available())
        {
            watchdog.Set(contextGroup, ExecutionTimeLimitSeconds, watchdog.Callback, &watchdog);
        }
#endif

#if __APPLE__
        if (__builtin_available(iOS 16.4, macOS 13.3, *))
        {
            JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);
        }
#endif

        Napi::Env env = Napi::Attach(globalContext);

        Run(env);

#if defined(JSRUNTIMEHOST_HAS_JSC_EXECUTION_TIME_LIMIT) || \
    defined(JSRUNTIMEHOST_LOOKUP_JSC_EXECUTION_TIME_LIMIT)
        if (watchdog.Available())
        {
            watchdog.Clear(contextGroup);
        }
#endif

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }

    void AppRuntime::ShutdownEnvironment(Napi::Env)
    {
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // JavaScriptCore drains Promise jobs at script boundaries, but Apple
        // platform work such as asynchronous WebAssembly compilation completes
        // through the current thread's run loop.
#if __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, true);
#endif
    }
}
