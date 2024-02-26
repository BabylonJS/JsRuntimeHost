#include "AppRuntime.h"
#include <exception>

#import <Foundation/NSObjCRuntime.h>

namespace Babylon
{
    void AppRuntime::DefaultUnhandledExceptionHandler(const std::exception& error)
    {
        NSLog(@"Uncaught Error: %s", error.what());
    }

    void AppRuntime::RunPlatformTier()
    {
        RunEnvironmentTier();
    }

    void AppRuntime::Execute(Dispatchable<void()> callback)
    {
        @autoreleasepool
        {
            callback();
        }
    }
}
