#include "WorkQueue.h"

#ifdef USE_QUICKJS
#ifdef _WIN32
#pragma warning(push)
// cast from int64 to int32
#pragma warning(disable : 4244)
#endif
#include <quickjs.h>
#ifdef _WIN32
#pragma warning(pop)
#endif
#endif

namespace Babylon
{
    WorkQueue::WorkQueue(std::function<void()> threadProcedure)
        : m_thread{std::move(threadProcedure)}
    {
    }

    WorkQueue::~WorkQueue()
    {
        if (m_suspensionLock.has_value())
        {
            Resume();
        }

        m_cancelSource.cancel();
        m_dispatcher.cancelled();

        m_thread.join();
    }

    void WorkQueue::Suspend()
    {
        auto suspensionMutex = std::make_shared<std::mutex>();
        m_suspensionLock.emplace(*suspensionMutex);
        Append([suspensionMutex{std::move(suspensionMutex)}](Napi::Env) {
            std::scoped_lock lock{*suspensionMutex};
        });
    }

    void WorkQueue::Resume()
    {
        m_suspensionLock.reset();
    }

    void WorkQueue::Run(Napi::Env env)
    {
        m_env = std::make_optional(env);
        m_dispatcher.set_affinity(std::this_thread::get_id());

        while (!m_cancelSource.cancelled())
        {
            m_dispatcher.blocking_tick(m_cancelSource);
#ifdef USE_QUICKJS
            // Process QuickJS microtasks (promise callbacks, etc.)
            JSContext* pending_ctx;
            int result;
            while ((result = JS_ExecutePendingJob(JS_GetRuntime(Napi::GetContext(env)), &pending_ctx)) > 0)
            {
                // Keep draining the microtask queue
            }
#endif
        }

        m_dispatcher.clear();
    }
}
