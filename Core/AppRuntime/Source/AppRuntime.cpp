#include "AppRuntime.h"
#include "WorkQueue.h"
#include <sstream>

namespace Babylon
{
    AppRuntime::AppRuntime()
        : AppRuntime{DefaultUnhandledExceptionHandler}
    {
    }

    AppRuntime::AppRuntime(std::function<void(const std::exception&)> unhandledExceptionHandler)
        : m_workQueue{std::make_unique<WorkQueue>([this] { RunPlatformTier(); })}
        , m_unhandledExceptionHandler{unhandledExceptionHandler}
    {
        Dispatch([this](Napi::Env env) {
            JsRuntime::CreateForJavaScript(env, [this](auto func) { Dispatch(std::move(func)); });
        });
    }

    AppRuntime::~AppRuntime()
    {
    }

    void AppRuntime::Run(Napi::Env env)
    {
        m_workQueue->Run(env);
    }

    void AppRuntime::Suspend()
    {
        m_workQueue->Suspend();
    }

    void AppRuntime::Resume()
    {
        m_workQueue->Resume();
    }

    std::string getStringPropertyFromError(Napi::Error error, const char* propertyName) {
        Napi::Value value = error.Get(propertyName);
        if (value.IsUndefined())
        {
            return "";
        }
        return value.ToString().Utf8Value();
    }

    int32_t getNumberPropertyFromError(Napi::Error error, const char* propertyName)
    {
        Napi::Value value = error.Get(propertyName);
        if (value.IsUndefined())
        {
            return -1;
        }
        return value.ToNumber().Int32Value();
    }

    void AppRuntime::Dispatch(Dispatchable<void(Napi::Env)> func)
    {
        m_workQueue->Append([this, func{std::move(func)}](Napi::Env env) mutable {
            Execute([this, env, func{std::move(func)}]() mutable {
                try
                {
                    func(env);
                }
                catch (const Napi::Error& error)
                {
                    std::string msg = error.Message();
                    
                    std::string source = getStringPropertyFromError(error, "source");
                    std::string url = getStringPropertyFromError(error, "url");
                    int32_t line = getNumberPropertyFromError(error, "line");
                    int32_t column = getNumberPropertyFromError(error, "column");
                    int32_t length = getNumberPropertyFromError(error, "length");
                    std::ostringstream ss{};
                    ss << "Error on line " << line << " and column " << column
                       << ": " << msg << ". Length: " << length << ". Source: " << source << ". URL: " << url << std::endl;

                    Napi::Error newError = Napi::Error::New(env, ss.str());
                    m_unhandledExceptionHandler(newError); 
                }
                catch (const std::exception& error)
                {
                    m_unhandledExceptionHandler(error);
                }
                catch (...)
                {
                    std::abort();
                }
            });
        });
    }
}
