#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace Babylon
{
    /// Native deadline queue used by setTimeout/setInterval and by the V8
    /// foreground task runner's delayed posts. Callbacks run on the scheduler
    /// thread; callers that need the JavaScript thread Dispatch themselves.
    class DeadlineScheduler final
    {
    public:
        using Id = int32_t;
        using Callback = std::function<void()>;
        using TimePoint = std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds>;

        DeadlineScheduler();
        ~DeadlineScheduler();

        DeadlineScheduler(const DeadlineScheduler&) = delete;
        DeadlineScheduler& operator=(const DeadlineScheduler&) = delete;

        Id Schedule(TimePoint when, Callback callback);
        Id Schedule(std::chrono::milliseconds delay, Callback callback);
        void Cancel(Id id);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
