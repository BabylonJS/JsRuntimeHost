#include "AppRuntime.h"
#include <napi/env.h>

#include <libplatform/libplatform.h>

#ifdef ENABLE_V8_INSPECTOR
#include <V8InspectorAgent.h>
#endif

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace Babylon
{
    namespace
    {
        // V8 hands work that finishes off-thread - most visibly asynchronous WebAssembly
        // compilation - back to the isolate by posting a task to the platform's *foreground*
        // task runner. Those tasks only run when someone calls v8::platform::PumpMessageLoop,
        // and the host's JavaScript thread sits in a blocking dispatcher wait, so nothing
        // would ever pump it: WebAssembly.compile/instantiate simply never settled, and
        // anything built on an Emscripten module hung forever.
        //
        // This platform wraps the default one and leaves it owning the queue (so task
        // ordering, nestability and delays keep V8's own semantics). All it adds is a
        // wake-up: whenever V8 posts foreground work, the AppRuntime dispatcher is nudged,
        // and the pump in AppRuntime::DrainMicrotasks then drains the queue on the
        // JavaScript thread.
        class DispatchingPlatform final : public v8::Platform
        {
        public:
            explicit DispatchingPlatform(std::unique_ptr<v8::Platform> inner)
                : m_inner{std::move(inner)}
            {
            }

            v8::Platform& Inner()
            {
                return *m_inner;
            }

            void SetWake(v8::Isolate* isolate, std::function<void()> wake)
            {
                std::scoped_lock lock{m_mutex};
                if (wake)
                {
                    m_wakes[isolate] = std::move(wake);
                }
                else
                {
                    m_wakes.erase(isolate);
                    m_taskRunners.erase(isolate);
                }
            }

            void Wake(v8::Isolate* isolate)
            {
                std::function<void()> wake;
                {
                    std::scoped_lock lock{m_mutex};
                    const auto entry = m_wakes.find(isolate);
                    if (entry == m_wakes.end())
                    {
                        return;
                    }
                    wake = entry->second;
                }
                wake();
            }

            v8::PageAllocator* GetPageAllocator() override { return m_inner->GetPageAllocator(); }
            v8::ThreadIsolatedAllocator* GetThreadIsolatedAllocator() override { return m_inner->GetThreadIsolatedAllocator(); }
            v8::ZoneBackingAllocator* GetZoneBackingAllocator() override { return m_inner->GetZoneBackingAllocator(); }
            void OnCriticalMemoryPressure() override { m_inner->OnCriticalMemoryPressure(); }
            int NumberOfWorkerThreads() override { return m_inner->NumberOfWorkerThreads(); }

            std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate) override
            {
                return GetForegroundTaskRunner(isolate, v8::TaskPriority::kUserBlocking);
            }

            std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate, v8::TaskPriority priority) override;

            void CallOnWorkerThread(std::unique_ptr<v8::Task> task) override { m_inner->CallOnWorkerThread(std::move(task)); }
            void CallBlockingTaskOnWorkerThread(std::unique_ptr<v8::Task> task) override { m_inner->CallBlockingTaskOnWorkerThread(std::move(task)); }
            void CallLowPriorityTaskOnWorkerThread(std::unique_ptr<v8::Task> task) override { m_inner->CallLowPriorityTaskOnWorkerThread(std::move(task)); }
            void CallDelayedOnWorkerThread(std::unique_ptr<v8::Task> task, double delayInSeconds) override { m_inner->CallDelayedOnWorkerThread(std::move(task), delayInSeconds); }
            bool IdleTasksEnabled(v8::Isolate* isolate) override { return m_inner->IdleTasksEnabled(isolate); }
            std::unique_ptr<v8::JobHandle> PostJob(v8::TaskPriority priority, std::unique_ptr<v8::JobTask> jobTask) override { return m_inner->PostJob(priority, std::move(jobTask)); }
            std::unique_ptr<v8::JobHandle> CreateJob(v8::TaskPriority priority, std::unique_ptr<v8::JobTask> jobTask) override { return m_inner->CreateJob(priority, std::move(jobTask)); }
            std::unique_ptr<v8::ScopedBlockingCall> CreateBlockingScope(v8::BlockingType blockingType) override { return m_inner->CreateBlockingScope(blockingType); }
            double MonotonicallyIncreasingTime() override { return m_inner->MonotonicallyIncreasingTime(); }
            int64_t CurrentClockTimeMilliseconds() override { return m_inner->CurrentClockTimeMilliseconds(); }
            double CurrentClockTimeMillis() override { return m_inner->CurrentClockTimeMillis(); }
            double CurrentClockTimeMillisecondsHighResolution() override { return m_inner->CurrentClockTimeMillisecondsHighResolution(); }
            StackTracePrinter GetStackTracePrinter() override { return m_inner->GetStackTracePrinter(); }
            v8::TracingController* GetTracingController() override { return m_inner->GetTracingController(); }
            void DumpWithoutCrashing() override { m_inner->DumpWithoutCrashing(); }
            v8::HighAllocationThroughputObserver* GetHighAllocationThroughputObserver() override { return m_inner->GetHighAllocationThroughputObserver(); }

        private:
            std::unique_ptr<v8::Platform> m_inner;
            std::mutex m_mutex;
            std::map<v8::Isolate*, std::function<void()>> m_wakes;
            std::map<v8::Isolate*, std::shared_ptr<v8::TaskRunner>> m_taskRunners;
        };

        class WakingTaskRunner final : public v8::TaskRunner
        {
        public:
            WakingTaskRunner(std::shared_ptr<v8::TaskRunner> inner, DispatchingPlatform& platform, v8::Isolate* isolate)
                : m_inner{std::move(inner)}
                , m_platform{platform}
                , m_isolate{isolate}
            {
            }

            void PostTask(std::unique_ptr<v8::Task> task) override
            {
                m_inner->PostTask(std::move(task));
                m_platform.Wake(m_isolate);
            }

            void PostNonNestableTask(std::unique_ptr<v8::Task> task) override
            {
                m_inner->PostNonNestableTask(std::move(task));
                m_platform.Wake(m_isolate);
            }

            void PostDelayedTask(std::unique_ptr<v8::Task> task, double delayInSeconds) override
            {
                m_inner->PostDelayedTask(std::move(task), delayInSeconds);
                m_platform.Wake(m_isolate);
            }

            void PostNonNestableDelayedTask(std::unique_ptr<v8::Task> task, double delayInSeconds) override
            {
                m_inner->PostNonNestableDelayedTask(std::move(task), delayInSeconds);
                m_platform.Wake(m_isolate);
            }

            void PostIdleTask(std::unique_ptr<v8::IdleTask> task) override
            {
                m_inner->PostIdleTask(std::move(task));
            }

            bool IdleTasksEnabled() override { return m_inner->IdleTasksEnabled(); }
            bool NonNestableTasksEnabled() const override { return m_inner->NonNestableTasksEnabled(); }
            bool NonNestableDelayedTasksEnabled() const override { return m_inner->NonNestableDelayedTasksEnabled(); }

        private:
            std::shared_ptr<v8::TaskRunner> m_inner;
            DispatchingPlatform& m_platform;
            v8::Isolate* m_isolate;
        };

        std::shared_ptr<v8::TaskRunner> DispatchingPlatform::GetForegroundTaskRunner(v8::Isolate* isolate, v8::TaskPriority)
        {
            // The priority-aware overload is not implemented by libplatform's DefaultPlatform in
            // this V8 version - the base class default returns nullptr - so always ask the inner
            // platform for the plain per-isolate runner. The wrapper is cached because V8 calls
            // this often and hands the result around by pointer.
            std::scoped_lock lock{m_mutex};
            auto& runner = m_taskRunners[isolate];
            if (!runner)
            {
                runner = std::make_shared<WakingTaskRunner>(m_inner->GetForegroundTaskRunner(isolate), *this, isolate);
            }
            return runner;
        }

        class Module final
        {
        public:
            Module(const char* executablePath)
            {
                v8::V8::InitializeICUDefaultLocation(executablePath);
                v8::V8::InitializeExternalStartupData(executablePath);
                m_platform = std::make_unique<DispatchingPlatform>(v8::platform::NewDefaultPlatform());
                v8::V8::InitializePlatform(m_platform.get());
                v8::V8::Initialize();
            }

            ~Module()
            {
                v8::V8::Dispose();
                v8::V8::DisposePlatform();
            }

            static void Initialize(const char* executablePath)
            {
                if (s_module == nullptr)
                {
                    s_module = std::make_unique<Module>(executablePath);
                }
            }

            static Module& Instance()
            {
                if (!s_module)
                {
                    throw std::runtime_error{"Module not available"};
                }

                return *s_module;
            }

            static Module* TryInstance()
            {
                return s_module.get();
            }

            DispatchingPlatform& Platform()
            {
                return *m_platform;
            }

            // v8::platform::PumpMessageLoop downcasts to the libplatform DefaultPlatform, so it
            // has to be handed the wrapped platform rather than the wrapper.
            v8::Platform& DefaultPlatform()
            {
                return m_platform->Inner();
            }

        private:
            std::unique_ptr<DispatchingPlatform> m_platform;

            static std::unique_ptr<Module> s_module;
        };

        std::unique_ptr<Module> Module::s_module;
    }

    void AppRuntime::RunEnvironmentTier(const char* executablePath)
    {
        // Create the isolate.
        Module::Initialize(executablePath);

        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        v8::Isolate* isolate = v8::Isolate::New(create_params);

        // Nudge the dispatcher whenever V8 posts foreground work for this isolate, so the
        // pump in DrainMicrotasks gets a chance to run it even when the app is otherwise
        // idle (no render loop, no timers). The flag collapses bursts of posts into a single
        // pending wake-up.
        //
        // The flag must be cleared in the dispatched callback, which Dispatch runs *before*
        // DrainMicrotasks pumps. Clearing it afterwards instead would lose wake-ups: a task
        // posted between the pump draining the queue and the flag being cleared would find
        // the flag still set, skip the dispatch, and then sit in the queue with nothing
        // scheduled to pump it. Clearing first can only ever cost one redundant no-op
        // dispatch, because such a post is already covered by the pump that follows.
        auto wakePending = std::make_shared<std::atomic_bool>(false);
        Module::Instance().Platform().SetWake(isolate, [this, wakePending]() {
            if (wakePending->exchange(true))
            {
                return;
            }

            Dispatch([wakePending](Napi::Env) { wakePending->store(false); });
        });

        // Use the isolate within a scope.
        {
            v8::Isolate::Scope isolate_scope{isolate};
            v8::HandleScope isolate_handle_scope{isolate};
            v8::Local<v8::Context> context = v8::Context::New(isolate);
            v8::Context::Scope context_scope{context};

            Napi::Env env = Napi::Attach(context);

#ifdef ENABLE_V8_INSPECTOR
            std::optional<V8InspectorAgent> agent;
            if (m_options.EnableDebugger)
            {
                agent.emplace(Module::Instance().Platform(), isolate, context, "JsRuntimeHost");
                agent->Start(5643, "JsRuntimeHost");

                if (m_options.WaitForDebugger)
                {
                    agent->WaitForDebugger();
                }
            }
#endif

            Run(env);

#ifdef ENABLE_V8_INSPECTOR
            if (agent.has_value())
            {
                agent->Stop();
            }
#endif

            Napi::Detach(env);
        }

        // Destroy the isolate.
        Module::Instance().Platform().SetWake(isolate, nullptr);

        // todo : GetArrayBufferAllocator not available?
        // delete isolate->GetArrayBufferAllocator();
        isolate->Dispose();
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // V8 auto-drains microtasks at the end of each script/callback when using the default
        // MicrotasksPolicy, but microtasks are not the whole story: work that V8 hands to the
        // v8::Platform completes on a background thread and then posts a *foreground* task to
        // settle its result on the isolate thread. Asynchronous WebAssembly compilation is the
        // visible case - WebAssembly.compile/instantiate/instantiateStreaming would never
        // resolve or reject, hanging any Emscripten module (and therefore anything built on
        // one) forever. Nothing else in the host drains that queue, so pump it here, after
        // every dispatched callback, which for a rendering app means at least once a frame.
        Module* module{Module::TryInstance()};
        v8::Isolate* isolate{v8::Isolate::GetCurrent()};
        if (module == nullptr || isolate == nullptr)
        {
            return;
        }

        // kDoNotWait: never block the JavaScript thread waiting for background work.
        while (v8::platform::PumpMessageLoop(&module->DefaultPlatform(), isolate, v8::platform::MessageLoopBehavior::kDoNotWait))
        {
        }
    }
}
