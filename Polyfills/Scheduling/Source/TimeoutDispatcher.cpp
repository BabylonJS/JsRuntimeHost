#include "TimeoutDispatcher.h"

#include <cassert>
#include <optional>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        using TimePoint = std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds>;

        TimePoint Now()
        {
            return std::chrono::time_point_cast<std::chrono::microseconds, std::chrono::steady_clock>(std::chrono::steady_clock::now());
        }
    }

    struct TimeoutDispatcher::Timeout
    {
        TimeoutId id;

        // Make this non-shared when JsRuntime::Dispatch supports it.
        std::shared_ptr<Napi::FunctionReference> function;

        TimePoint time;

        std::optional<std::chrono::milliseconds> interval;

        Timeout(TimeoutId id, std::shared_ptr<Napi::FunctionReference> function, TimePoint time, std::optional<std::chrono::milliseconds> interval)
            : id{id}
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
        , m_thread{std::thread{&TimeoutDispatcher::ThreadFunction, this}}
    {
    }

    TimeoutDispatcher::~TimeoutDispatcher()
    {
        {
            std::unique_lock<std::recursive_mutex> lk{m_mutex};
            m_idMap.clear();
            m_timeMap.clear();
        }

        m_shutdown = true;
        m_condVariable.notify_one();
        m_thread.join();
    }

    TimeoutDispatcher::TimeoutId TimeoutDispatcher::Dispatch(std::shared_ptr<Napi::FunctionReference> function, std::chrono::milliseconds delay, bool repeat)
    {
        return DispatchImpl(function, delay, repeat, 0);
    }

    TimeoutDispatcher::TimeoutId TimeoutDispatcher::DispatchImpl(std::shared_ptr<Napi::FunctionReference> function, std::chrono::milliseconds delay, bool repeat, TimeoutId id)
    {
        if (delay.count() < 0)
        {
            delay = std::chrono::milliseconds{0};
        }

        std::unique_lock<std::recursive_mutex> lk{m_mutex};

        if (id == 0)
        {
            id = NextTimeoutId();
        }
        const auto earliestTime = m_timeMap.empty() ? TimePoint::max() : m_timeMap.cbegin()->second->time;
        const auto time = Now() + delay;
        const auto result = m_idMap.insert({id, std::make_unique<Timeout>(id, std::move(function), time, repeat ? std::make_optional<std::chrono::milliseconds>(delay) : std::nullopt)});
        m_timeMap.insert({time, result.first->second.get()});

        if (time <= earliestTime)
        {
            m_condVariable.notify_one();
        }

        return id;
    }

    void TimeoutDispatcher::Clear(TimeoutId id)
    {
        std::unique_lock<std::recursive_mutex> lk{m_mutex};
        const auto itId = m_idMap.find(id);
        if (itId != m_idMap.end())
        {
            const auto& timeout = itId->second;
            const auto timeRange = m_timeMap.equal_range(timeout->time);

            // Remove any pending entries that have not yet been dispatched.
            for (auto itTime = timeRange.first; itTime != timeRange.second; itTime++)
            {
                if (itTime->second->id == id)
                {
                    m_timeMap.erase(itTime);
                    break;
                }
            }

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

    void TimeoutDispatcher::ThreadFunction()
    {
        while (!m_shutdown)
        {
            std::unique_lock<std::recursive_mutex> lk{m_mutex};
            TimePoint nextTimePoint{};

            while (!m_timeMap.empty())
            {
                nextTimePoint = m_timeMap.begin()->second->time;
                if (nextTimePoint <= Now())
                {
                    break;
                }

                m_condVariable.wait_until(lk, nextTimePoint);
            }

            while (!m_timeMap.empty() && m_timeMap.begin()->second->time == nextTimePoint)
            {
                const auto id = m_timeMap.begin()->second->id;
                m_timeMap.erase(m_timeMap.begin());
                const auto repeat = m_idMap[id]->interval.has_value();
                if (repeat)
                {
                    const auto timeout = std::move(m_idMap.extract(id).mapped());
                    DispatchImpl(std::move(timeout->function), *timeout->interval, true, timeout->id);
                }
                CallFunction(id);
            }

            while (!m_shutdown && m_timeMap.empty())
            {
                m_condVariable.wait(lk);
            }
        }
    }

    void TimeoutDispatcher::CallFunction(TimeoutId id)
    {
        m_runtime.Dispatch([id, this](Napi::Env) {
            std::shared_ptr<Napi::FunctionReference> function{};
            {
                std::unique_lock<std::recursive_mutex> lk{m_mutex};
                const auto it = m_idMap.find(id);
                if (it != m_idMap.end())
                {
                    const auto repeat = it->second->interval.has_value();
                    if (repeat)
                    {
                        function = it->second->function;
                    }
                    else
                    {
                        const auto timeout = std::move(m_idMap.extract(id).mapped());
                        function = std::move(timeout->function);
                    }
                }
            }

            if (function)
            {
                function->Call({});
            }
        });
    }
}
