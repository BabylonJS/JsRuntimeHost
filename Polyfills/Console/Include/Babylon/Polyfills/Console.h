#pragma once

#include <napi/env.h>
#include <Babylon/Api.h>

namespace Babylon::Polyfills::Console
{
    /**
     * Importance level of messages sent via logging callbacks.
     */
    enum class LogLevel
    {
        Log,
        Warn,
        Error,
    };

    /**
     * Called for each `console.log` / `console.warn` / `console.error` invocation.
     *
     * @param message   Formatted message produced by joining the call arguments (like browsers do).
     * @param logLevel  Importance of the message.
     * @param jsStack   For `LogLevel::Error`, the JavaScript callstack captured at the
     *                  `console.error` call site (raw `Error.stack` string -- the format is engine-
     *                  defined and typically starts with the literal `Error\n`). Empty string for
     *                  `Log` and `Warn` (capturing a stack on every `console.log` is too expensive).
     */
    using CallbackT = std::function<void BABYLON_API (const char* message, LogLevel logLevel, const char* jsStack)>;

    void BABYLON_API Initialize(Napi::Env env, CallbackT callback);
}
