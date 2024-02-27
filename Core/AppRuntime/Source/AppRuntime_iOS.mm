#include "AppRuntime.h"
#include <exception>
#include <sstream>
#import <Foundation/NSObjCRuntime.h>

namespace Babylon
{
    void AppRuntime::RunPlatformTier()
    {
        RunEnvironmentTier();
    }

    void AppRuntime::DefaultUnhandledExceptionHandler(const std::exception& error)
    {
        std::stringstream ss{};
        ss << error.what() << std::endl;

        try
        {
            throw;
        }
        catch (const Napi::Error& error)
        {
            ss << GetErrorInfos(error) << std::endl;
        }

        NSLog(@"Uncaught Error: %s", ss.str().data());
    }

    void AppRuntime::Execute(Dispatchable<void()> callback)
    {
        @autoreleasepool
        {
            callback();
        }
    }
}
