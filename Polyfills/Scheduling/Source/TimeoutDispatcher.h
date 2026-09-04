#pragma once

#include <Babylon/DeadlineScheduler.h>
#include <Babylon/JsRuntime.h>
#include <napi/napi.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Babylon::Polyfills::Internal
{
    class TimeoutDispatcher
    {
        using TimeoutId = int32_t;
        struct Timeout;

    public:
        TimeoutDispatcher(Babylon::JsRuntime& runtime);
        ~TimeoutDispatcher();

        TimeoutId Dispatch(std::shared_ptr<Napi::FunctionReference> function, std::chrono::milliseconds delay, bool repeat = false);
        void Clear(TimeoutId id);

    private:
        using TimePoint = DeadlineScheduler::TimePoint;

        TimeoutId NextTimeoutId();
        void CallFunction(TimeoutId id, uint64_t sequence);
        void Rearm(TimeoutId id, uint64_t sequence, TimePoint scheduledTime, std::chrono::milliseconds interval);

        Babylon::JsRuntime& m_runtime;
        DeadlineScheduler& m_scheduler;
        std::recursive_mutex m_mutex{};
        TimeoutId m_lastTimeoutId{0};
        uint64_t m_lastSequence{0};
        std::unordered_map<TimeoutId, std::unique_ptr<Timeout>> m_idMap;
    };
}
