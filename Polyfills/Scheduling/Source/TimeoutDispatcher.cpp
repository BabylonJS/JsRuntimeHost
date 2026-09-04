#include "TimeoutDispatcher.h"

#include <optional>
#include <stdexcept>
#include <utility>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        DeadlineScheduler::TimePoint Now()
        {
            return std::chrono::time_point_cast<std::chrono::microseconds, std::chrono::steady_clock>(std::chrono::steady_clock::now());
        }
    }

    struct TimeoutDispatcher::Timeout
    {
        TimeoutId id;
        uint64_t sequence;
        std::shared_ptr<Napi::FunctionReference> function;
        TimePoint time;
        std::optional<std::chrono::milliseconds> interval;
        DeadlineScheduler::Id scheduleId{};

        Timeout(TimeoutId id, uint64_t sequence, std::shared_ptr<Napi::FunctionReference> function, TimePoint time, std::optional<std::chrono::milliseconds> interval)
            : id{id}
            , sequence{sequence}
            , function{std::move(function)}
            , time{time}
            , interval{interval}
        {
        }

        Timeout(const Timeout&) = delete;
        Timeout(Timeout&&) = delete;
    };

    TimeoutDispatcher::TimeoutDispatcher(Babylon::JsRuntime& runtime)
        : m_runtime{runtime}
        , m_scheduler{runtime.GetDeadlineScheduler()}
    {
    }

    TimeoutDispatcher::~TimeoutDispatcher()
    {
        std::unique_lock<std::recursive_mutex> lk{m_mutex};
        for (auto& [id, timeout] : m_idMap)
        {
            m_scheduler.Cancel(timeout->scheduleId);
        }
        m_idMap.clear();
    }

    TimeoutDispatcher::TimeoutId TimeoutDispatcher::Dispatch(std::shared_ptr<Napi::FunctionReference> function, std::chrono::milliseconds delay, bool repeat)
    {
        if (delay.count() < 0)
        {
            delay = std::chrono::milliseconds{0};
        }

        std::unique_lock<std::recursive_mutex> lk{m_mutex};

        const auto id = NextTimeoutId();
        const auto sequence = ++m_lastSequence;
        const auto time = Now() + delay;
        auto timeout = std::make_unique<Timeout>(id, sequence, std::move(function), time, repeat ? std::make_optional<std::chrono::milliseconds>(delay) : std::nullopt);
        const auto [it, inserted] = m_idMap.try_emplace(id, std::move(timeout));
        if (!inserted)
        {
            throw std::logic_error{"TimeoutDispatcher: NextTimeoutId returned a duplicate id"};
        }

        it->second->scheduleId = m_scheduler.Schedule(time, [this, id, sequence]() {
            CallFunction(id, sequence);
        });

        return id;
    }

    void TimeoutDispatcher::Clear(TimeoutId id)
    {
        std::unique_lock<std::recursive_mutex> lk{m_mutex};
        const auto itId = m_idMap.find(id);
        if (itId != m_idMap.end())
        {
            m_scheduler.Cancel(itId->second->scheduleId);
            m_idMap.erase(itId);
        }
    }

    TimeoutDispatcher::TimeoutId TimeoutDispatcher::NextTimeoutId()
    {
        while (true)
        {
            ++m_lastTimeoutId;

            if (m_lastTimeoutId <= 0)
            {
                m_lastTimeoutId = 1;
            }

            if (m_idMap.find(m_lastTimeoutId) == m_idMap.end())
            {
                return m_lastTimeoutId;
            }
        }
    }

    void TimeoutDispatcher::CallFunction(TimeoutId id, uint64_t sequence)
    {
        m_runtime.Dispatch([id, sequence, this](Napi::Env) {
            std::shared_ptr<Napi::FunctionReference> function{};
            std::optional<std::chrono::milliseconds> interval{};
            TimePoint scheduledTime{};
            {
                std::unique_lock<std::recursive_mutex> lk{m_mutex};
                const auto it = m_idMap.find(id);
                if (it == m_idMap.end() || it->second->sequence != sequence)
                {
                    // Cleared before the callback could run, or the id has since
                    // been reused by an unrelated timeout.
                    return;
                }

                interval = it->second->interval;
                scheduledTime = it->second->time;

                if (interval.has_value())
                {
                    function = it->second->function;
                }
                else
                {
                    const auto timeout = std::move(m_idMap.extract(id).mapped());
                    function = std::move(timeout->function);
                }
            }

            if (function)
            {
                try
                {
                    function->Call({});
                }
                catch (const Napi::Error& error)
                {
                    // A throwing tick must not silently stop the interval, which
                    // is both the pre-existing behavior and what browsers do.
                    // Re-arm first, then re-raise the error as a pending JS
                    // exception so JsRuntime::Dispatch still surfaces it.
                    if (interval.has_value())
                    {
                        Rearm(id, sequence, scheduledTime, *interval);
                    }

                    error.ThrowAsJavaScriptException();
                    return;
                }
            }

            if (interval.has_value())
            {
                Rearm(id, sequence, scheduledTime, *interval);
            }
        });
    }

    // Re-arms a repeating timeout. Called on the JS thread once the callback has
    // returned, so a repeating timeout can never have more than one invocation
    // queued at a time.
    void TimeoutDispatcher::Rearm(TimeoutId id, uint64_t sequence, TimePoint scheduledTime, std::chrono::milliseconds interval)
    {
        std::unique_lock<std::recursive_mutex> lk{m_mutex};

        const auto it = m_idMap.find(id);
        if (it == m_idMap.end() || it->second->sequence != sequence)
        {
            // Cleared from within its own callback, or the id has since been
            // reused by an unrelated timeout.
            return;
        }

        // Anchor the next deadline to the previous scheduled time so that a long
        // running callback does not accumulate drift, but never schedule into the
        // past.
        const auto now = Now();
        auto nextTime = scheduledTime + interval;
        if (nextTime < now)
        {
            nextTime = now;
        }

        it->second->time = nextTime;
        it->second->scheduleId = m_scheduler.Schedule(nextTime, [this, id, sequence]() {
            CallFunction(id, sequence);
        });
    }
}