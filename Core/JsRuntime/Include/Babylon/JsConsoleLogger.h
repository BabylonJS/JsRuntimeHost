#pragma once

#include <napi/env.h>

namespace Babylon
{
    /**
     * Utility struct to log messages to the JavaScript console.
     *
     * Native code that writes diagnostics to stdout/stderr is invisible on the platforms
     * where diagnostics matter most: on Android and iOS there is no attached terminal, so
     * the message reaches nobody. Routing through the JS console instead puts native
     * messages in the same place as the script's own, wherever the host has directed it.
     *
     * Each call is a no-op if no console object, or no such method on it, is present.
     */
    struct JsConsoleLogger final
    {
        JsConsoleLogger() = delete;

        static void LogInfo(Napi::Env env, const char* message);
        static void LogWarn(Napi::Env env, const char* message);
        static void LogError(Napi::Env env, const char* message);
    };
}
