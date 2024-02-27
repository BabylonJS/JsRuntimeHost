#include "WorkQueue.h"
#include "AppRuntime.h"
#include <exception>
#include <iostream>

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

        std::cerr << "Uncaught Error: " << ss.str().data() << std::endl;
    }

    void AppRuntime::Execute(Dispatchable<void()> callback)
    {
        callback();
    }
}
