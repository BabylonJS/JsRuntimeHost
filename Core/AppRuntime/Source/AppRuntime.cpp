#include "AppRuntime.h"
#include <cassert>

namespace Babylon
{
    AppRuntime::AppRuntime() :
        AppRuntime{{}}
    {
    }

    AppRuntime::AppRuntime(Options options)
        : m_options{std::move(options)}
        , m_thread{[this] { RunPlatformTier(); }}
    {
        Dispatch([this](Napi::Env env) {
            JsRuntime::CreateForJavaScript(env, [this](auto func) { Dispatch(std::move(func)); });
        });
    }

    AppRuntime::~AppRuntime()
    {
        if (m_suspensionLock.has_value())
        {
            m_suspensionLock.reset();
        }

        // See #147 for details on this shutdown sequence.
        m_cancelSource.cancel();
        Append([](Napi::Env) {});

        m_thread.join();
    }

    void AppRuntime::Run(Napi::Env env)
    {
        m_env = std::make_optional(env);

        m_dispatcher.set_affinity(std::this_thread::get_id());

        while (!m_cancelSource.cancelled())
        {
            m_dispatcher.blocking_tick(m_cancelSource);
        }

        // The dispatcher can be non-empty if something is dispatched after cancellation.
        m_dispatcher.clear();
    }

    void AppRuntime::Suspend()
    {
        auto suspensionMutex = std::make_shared<std::mutex>();
        m_suspensionLock.emplace(*suspensionMutex);
        Append([suspensionMutex{std::move(suspensionMutex)}](Napi::Env) {
            std::scoped_lock lock{*suspensionMutex};
        });
    }

    void AppRuntime::Resume()
    {
        m_suspensionLock.reset();
    }

    void AppRuntime::Dispatch(Dispatchable<void(Napi::Env)> func)
    {
        Append([this, func{std::move(func)}](Napi::Env env) mutable {
            Execute([this, env, func{std::move(func)}]() mutable {
                try
                {
                    func(env);
                }
                catch (const Napi::Error& error)
                {
                    m_options.UnhandledExceptionHandler(error);
                }
                catch (...)
                {
                    assert(false);
                    std::abort();
                }
            });
        });
    }
}
