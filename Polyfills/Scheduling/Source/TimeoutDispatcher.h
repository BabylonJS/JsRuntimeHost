#pragma once

#include <Babylon/JsRuntime.h>
#include <napi/napi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <thread>

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
        using TimePoint = std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds>;

        TimeoutId DispatchImpl(std::shared_ptr<Napi::FunctionReference> function, std::chrono::milliseconds delay, bool repeat, TimeoutId id);

        TimeoutId NextTimeoutId();
        void ThreadFunction();
        void CallFunction(TimeoutId id);

        Babylon::JsRuntime& m_runtime;
        std::recursive_mutex m_mutex{};
        std::condition_variable_any m_condVariable{};
        TimeoutId m_lastTimeoutId{0};
        std::unordered_map<TimeoutId, std::unique_ptr<Timeout>> m_idMap;
        std::multimap<TimePoint, Timeout*> m_timeMap;
        std::atomic<bool> m_shutdown{false};
        std::thread m_thread;
    };
}
