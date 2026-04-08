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

        // Cancel immediately so pending work is dropped promptly, then append
        // a no-op work item to wake the worker thread from blocking_tick. The
        // no-op goes through push() which acquires the queue mutex, avoiding
        // the race where a bare notify_all() can be missed by wait().
        //
        // NOTE: This preserves the existing shutdown behavior where pending
        // callbacks are dropped on cancellation. A more complete solution
        // would add cooperative shutdown (e.g. NotifyDisposing/Rundown) so
        // consumers can finish cleanup work before the runtime is destroyed.
        m_cancelSource.cancel();
        Append([](Napi::Env) {});

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
