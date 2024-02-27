#include "AppRuntime.h"

#include <Windows.h>

#include <exception>
#include <sstream>

namespace Babylon
{
    void AppRuntime::RunPlatformTier()
    {
        RunEnvironmentTier();
    }

    void AppRuntime::DefaultUnhandledExceptionHandler(const std::exception& error)
    {
        std::stringstream ss{};
        ss << "Uncaught Error: " << error.what() << std::endl;

        try
        {
            throw;
        }
        catch (const Napi::Error& error)
        {
            ss << GetErrorInfos(error) << std::endl;
        }

        OutputDebugStringA(ss.str().data());
    }

    void AppRuntime::Execute(Dispatchable<void()> callback)
    {
        callback();
    }
}
