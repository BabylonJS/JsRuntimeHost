#include "AppRuntime.h"
#import <Foundation/NSObjCRuntime.h>

namespace Babylon
{
    void AppRuntime::DefaultUnhandledExceptionHandler(const Napi::Error& error)
    {
        NSLog(@"[Uncaught Error] %s", Napi::GetErrorString(error).data());
    }

    void AppRuntime::RunPlatformTier()
    {
        RunEnvironmentTier();
    }

    void AppRuntime::RunEnvironmentTier(const char*)
    {
        auto globalContext = JSGlobalContextCreateInGroup(nullptr, nullptr);

        if (@available(iOS 16.4, *)) {
            JSGlobalContextSetInspectable(globalContext, m_options.EnableDebugger);
        }

        Napi::Env env = Napi::Attach(globalContext);

        Run(env);

        JSGlobalContextRelease(globalContext);

        // Detach must come after JSGlobalContextRelease since it triggers finalizers which require env.
        Napi::Detach(env);
    }

    void AppRuntime::Execute(Dispatchable<void()> callback)
    {
        @autoreleasepool
        {
            callback();
        }
    }
}
