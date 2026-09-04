#include "DeadlineScheduler.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Babylon
{
    namespace
    {
        DeadlineScheduler::TimePoint Now()
        {
            return std::chrono::time_point_cast<std::chrono::microseconds, std::chrono::steady_clock>(std::chrono::steady_clock::now());
        }
    }

    class DeadlineScheduler::Impl
    {
    public:
        Impl()
            : m_thread{&Impl::ThreadFunction, this}
        {
        }

        ~Impl()
        {
            {
                std::unique_lock<std::mutex> lk{m_mutex};
                m_shutdown = true;
                m_idMap.clear();
                m_timeMap.clear();
            }

            m_condVariable.notify_one();
            m_thread.join();
        }

        Id Schedule(TimePoint when, Callback callback)
        {
            std::unique_lock<std::mutex> lk{m_mutex};
            if (m_shutdown)
            {
                throw std::runtime_error{"DeadlineScheduler: Schedule after shutdown"};
            }

            const auto earliestTime = m_timeMap.empty() ? TimePoint::max() : m_timeMap.begin()->first;
            const auto id = NextId();
            auto item = std::make_unique<Item>(id, when, std::move(callback));
            Item* const raw = item.get();
            const auto [it, inserted] = m_idMap.try_emplace(id, std::move(item));
            if (!inserted)
            {
                throw std::logic_error{"DeadlineScheduler: NextId returned a duplicate id"};
            }
            m_timeMap.insert({when, raw});

            if (when <= earliestTime)
            {
                m_condVariable.notify_one();
            }

            return id;
        }

        void Cancel(Id id)
        {
            std::unique_lock<std::mutex> lk{m_mutex};
            const auto it = m_idMap.find(id);
            if (it == m_idMap.end())
            {
                return;
            }

            const auto timeRange = m_timeMap.equal_range(it->second->time);
            for (auto itTime = timeRange.first; itTime != timeRange.second; ++itTime)
            {
                if (itTime->second->id == id)
                {
                    m_timeMap.erase(itTime);
                    break;
                }
            }

            m_idMap.erase(it);
        }

    private:
        struct Item
        {
            Id id;
            TimePoint time;
            Callback callback;

            Item(Id id, TimePoint time, Callback callback)
                : id{id}
                , time{time}
                , callback{std::move(callback)}
            {
            }
        };

        Id NextId()
        {
            while (true)
            {
                ++m_lastId;
                if (m_lastId <= 0)
                {
                    m_lastId = 1;
                }

                if (m_idMap.find(m_lastId) == m_idMap.end())
                {
                    return m_lastId;
                }
            }
        }

        void ThreadFunction()
        {
            while (!m_shutdown)
            {
                std::unique_lock<std::mutex> lk{m_mutex};
                while (!m_shutdown && m_timeMap.empty())
                {
                    m_condVariable.wait(lk);
                }

                if (m_shutdown)
                {
                    return;
                }

                const auto nextTimePoint = m_timeMap.begin()->first;
                if (nextTimePoint > Now())
                {
                    m_condVariable.wait_until(lk, nextTimePoint);
                    if (m_shutdown)
                    {
                        return;
                    }
                }

                std::vector<Callback> due;
                while (!m_timeMap.empty() && m_timeMap.begin()->first <= Now())
                {
                    Item* const item = m_timeMap.begin()->second;
                    due.push_back(std::move(item->callback));
                    m_idMap.erase(item->id);
                    m_timeMap.erase(m_timeMap.begin());
                }

                lk.unlock();
                for (auto& callback : due)
                {
                    if (callback)
                    {
                        callback();
                    }
                }
            }
        }

        std::mutex m_mutex{};
        std::condition_variable m_condVariable{};
        Id m_lastId{0};
        std::unordered_map<Id, std::unique_ptr<Item>> m_idMap;
        std::multimap<TimePoint, Item*> m_timeMap;
        std::atomic<bool> m_shutdown{false};
        std::thread m_thread;
    };

    DeadlineScheduler::DeadlineScheduler()
        : m_impl{std::make_unique<Impl>()}
    {
    }

    DeadlineScheduler::~DeadlineScheduler() = default;

    DeadlineScheduler::Id DeadlineScheduler::Schedule(TimePoint when, Callback callback)
    {
        return m_impl->Schedule(when, std::move(callback));
    }

    DeadlineScheduler::Id DeadlineScheduler::Schedule(std::chrono::milliseconds delay, Callback callback)
    {
        if (delay.count() < 0)
        {
            delay = std::chrono::milliseconds{0};
        }

        return Schedule(Now() + delay, std::move(callback));
    }

    void DeadlineScheduler::Cancel(Id id)
    {
        m_impl->Cancel(id);
    }
}
