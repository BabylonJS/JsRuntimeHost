#include <Babylon/JsConsoleLogger.h>

namespace Babylon
{
    namespace
    {
        void LogMethod(Napi::Env env, const char* methodName, const char* message)
        {
            try
            {
                auto console = env.Global().Get("console");

                if (console.IsObject())
                {
                    auto consoleLog{console.ToObject().Get(methodName)};

                    if (consoleLog.IsFunction())
                    {
                        auto consoleLogFunction = consoleLog.As<Napi::Function>();
                        auto messageStr = Napi::String::New(env, message);
                        consoleLogFunction.Call(console, {messageStr});
                    }
                }
            }
            catch (...)
            {
            }

            // Every step above can fail on script the host does not control: `console` and the
            // method can be accessors that throw, and the call itself is arbitrary user code.
            // N-API also leaves a pending exception on `env` independently of throwing a C++
            // exception, so swallowing the C++ side is not enough. Returning with one pending
            // would surface the failure at some unrelated later point in the caller, which is
            // never an acceptable outcome for a diagnostic helper.
            if (env.IsExceptionPending())
            {
                (void)env.GetAndClearPendingException();
            }
        }
    }

    void JsConsoleLogger::LogInfo(Napi::Env env, const char* message)
    {
        LogMethod(env, "log", message);
    }

    void JsConsoleLogger::LogWarn(Napi::Env env, const char* message)
    {
        LogMethod(env, "warn", message);
    }

    void JsConsoleLogger::LogError(Napi::Env env, const char* message)
    {
        LogMethod(env, "error", message);
    }
}
