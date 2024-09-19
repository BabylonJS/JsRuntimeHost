#include "WorkQueue.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
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

    void WorkQueue::Resume() { m_suspensionLock.reset(); }

    void WorkQueue::Run(Napi::Env env)
    {
        m_env = std::make_optional(env);
        m_dispatcher.set_affinity(std::this_thread::get_id());

        while (!m_cancelSource.cancelled())
        {
#if defined(__EMSCRIPTEN__)
            if (!m_dispatcher.tick(m_cancelSource))
            {
                // Do not block when using emscripten. We must ensure
                // to yield from the call stack so that javascript promises may
                // execute from the microqueue. This requires ASYNCIFY (tested)
                // or JSPI (untested).
                emscripten_sleep(0);
            }
#else
            m_dispatcher.blocking_tick(m_cancelSource);
#endif
        }

        m_dispatcher.clear();
    }
} // namespace Babylon
