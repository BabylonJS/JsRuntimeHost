#include "AppRuntime.h"
#include <napi/env.h>

#include <Babylon/DeadlineScheduler.h>

#include <libplatform/libplatform.h>
#include <v8-version.h>

// Android builds against V8 11.0, desktop against 11.9. A few v8::Platform members below do not
// exist in 11.0, and overriding a method the base class does not declare is a hard error, so they
// are gated. Where a member is gated out, v8::Platform's own default is used instead of forwarding
// to the inner platform; all of them are optional hooks whose defaults are benign (null allocator,
// null blocking scope, clock values derived from CurrentClockTimeMillis).
#define JSRH_V8_AT_LEAST(major, minor) \
    (V8_MAJOR_VERSION > (major) || (V8_MAJOR_VERSION == (major) && V8_MINOR_VERSION >= (minor)))


#ifdef ENABLE_V8_INSPECTOR
#include <V8InspectorAgent.h>
#endif

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Babylon
{
    namespace
    {
        // V8 hands work that finishes off-thread - most visibly asynchronous WebAssembly
        // compilation - back to the isolate by posting a task to the platform's *foreground*
        // task runner. Chromium runs those as `v8::Task::Run` on the host task runner. The
        // host JavaScript thread is AppRuntime's dispatcher, so this platform's foreground
        // runner posts the task itself instead of parking it on libplatform's queue and
        // pumping later. Delayed posts reuse the DeadlineScheduler behind setTimeout.
        class DispatchingPlatform final : public v8::Platform
        {
        public:
            explicit DispatchingPlatform(std::unique_ptr<v8::Platform> inner)
                : m_inner{std::move(inner)}
            {
            }

            struct Host
            {
                AppRuntime* runtime{};
                DeadlineScheduler* scheduler{};
            };

            // Isolate::New can ask for a foreground runner before SetHost has the
            // isolate pointer. Posts from this thread during construction fall back
            // to the thread-local host set around New.
            void SetCurrentHost(AppRuntime* runtime, DeadlineScheduler* scheduler)
            {
                t_currentRuntime = runtime;
                t_currentScheduler = scheduler;
            }

            void SetHost(v8::Isolate* isolate, AppRuntime* runtime, DeadlineScheduler* scheduler)
            {
                std::shared_ptr<v8::TaskRunner> runner;
                {
                    std::scoped_lock lock{m_mutex};
                    if (runtime != nullptr && scheduler != nullptr)
                    {
                        m_hosts[isolate] = Host{runtime, scheduler};
                    }
                    else
                    {
                        m_hosts.erase(isolate);
                        auto entry = m_taskRunners.find(isolate);
                        if (entry != m_taskRunners.end())
                        {
                            runner = std::move(entry->second);
                            m_taskRunners.erase(entry);
                        }
                    }
                }
            }

            Host GetHost(v8::Isolate* isolate)
            {
                std::scoped_lock lock{m_mutex};
                const auto entry = m_hosts.find(isolate);
                if (entry != m_hosts.end())
                {
                    return entry->second;
                }
                return {t_currentRuntime, t_currentScheduler};
            }

            v8::PageAllocator* GetPageAllocator() override { return m_inner->GetPageAllocator(); }
#if JSRH_V8_AT_LEAST(11, 9)
            v8::ThreadIsolatedAllocator* GetThreadIsolatedAllocator() override { return m_inner->GetThreadIsolatedAllocator(); }
#endif
            v8::ZoneBackingAllocator* GetZoneBackingAllocator() override { return m_inner->GetZoneBackingAllocator(); }
            void OnCriticalMemoryPressure() override { m_inner->OnCriticalMemoryPressure(); }
            int NumberOfWorkerThreads() override { return m_inner->NumberOfWorkerThreads(); }

            std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate) override
            {
                return WrapForegroundTaskRunner(isolate);
            }

#if JSRH_V8_AT_LEAST(11, 9)
            std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate, v8::TaskPriority) override
            {
                return WrapForegroundTaskRunner(isolate);
            }
#endif

            void CallOnWorkerThread(std::unique_ptr<v8::Task> task) override { m_inner->CallOnWorkerThread(std::move(task)); }
            void CallBlockingTaskOnWorkerThread(std::unique_ptr<v8::Task> task) override { m_inner->CallBlockingTaskOnWorkerThread(std::move(task)); }
            void CallLowPriorityTaskOnWorkerThread(std::unique_ptr<v8::Task> task) override { m_inner->CallLowPriorityTaskOnWorkerThread(std::move(task)); }
            void CallDelayedOnWorkerThread(std::unique_ptr<v8::Task> task, double delayInSeconds) override { m_inner->CallDelayedOnWorkerThread(std::move(task), delayInSeconds); }
            bool IdleTasksEnabled(v8::Isolate*) override { return false; }
            std::unique_ptr<v8::JobHandle> PostJob(v8::TaskPriority priority, std::unique_ptr<v8::JobTask> jobTask) override { return m_inner->PostJob(priority, std::move(jobTask)); }
            std::unique_ptr<v8::JobHandle> CreateJob(v8::TaskPriority priority, std::unique_ptr<v8::JobTask> jobTask) override { return m_inner->CreateJob(priority, std::move(jobTask)); }
#if JSRH_V8_AT_LEAST(11, 9)
            std::unique_ptr<v8::ScopedBlockingCall> CreateBlockingScope(v8::BlockingType blockingType) override { return m_inner->CreateBlockingScope(blockingType); }
#endif
            double MonotonicallyIncreasingTime() override { return m_inner->MonotonicallyIncreasingTime(); }
#if JSRH_V8_AT_LEAST(11, 9)
            int64_t CurrentClockTimeMilliseconds() override { return m_inner->CurrentClockTimeMilliseconds(); }
#endif
            double CurrentClockTimeMillis() override { return m_inner->CurrentClockTimeMillis(); }
#if JSRH_V8_AT_LEAST(11, 9)
            double CurrentClockTimeMillisecondsHighResolution() override { return m_inner->CurrentClockTimeMillisecondsHighResolution(); }
#endif
            StackTracePrinter GetStackTracePrinter() override { return m_inner->GetStackTracePrinter(); }
            v8::TracingController* GetTracingController() override { return m_inner->GetTracingController(); }
            void DumpWithoutCrashing() override { m_inner->DumpWithoutCrashing(); }
            v8::HighAllocationThroughputObserver* GetHighAllocationThroughputObserver() override { return m_inner->GetHighAllocationThroughputObserver(); }

        private:
            std::shared_ptr<v8::TaskRunner> WrapForegroundTaskRunner(v8::Isolate* isolate);

            std::unique_ptr<v8::Platform> m_inner;
            std::mutex m_mutex;
            std::map<v8::Isolate*, Host> m_hosts;
            std::map<v8::Isolate*, std::shared_ptr<v8::TaskRunner>> m_taskRunners;

            static thread_local AppRuntime* t_currentRuntime;
            static thread_local DeadlineScheduler* t_currentScheduler;
        };

        thread_local AppRuntime* DispatchingPlatform::t_currentRuntime{};
        thread_local DeadlineScheduler* DispatchingPlatform::t_currentScheduler{};

        class DispatchingTaskRunner final : public v8::TaskRunner
        {
        public:
            DispatchingTaskRunner(DispatchingPlatform& platform, v8::Isolate* isolate)
                : m_platform{platform}
                , m_isolate{isolate}
            {
            }

            ~DispatchingTaskRunner() override
            {
                std::scoped_lock lock{m_mutex};
                for (const auto& pending : m_pending)
                {
                    pending.scheduler->Cancel(pending.id);
                }
                m_pending.clear();
            }

            void PostTask(std::unique_ptr<v8::Task> task) override
            {
                PostImmediate(std::move(task));
            }

            void PostNonNestableTask(std::unique_ptr<v8::Task> task) override
            {
                PostImmediate(std::move(task));
            }

            void PostDelayedTask(std::unique_ptr<v8::Task> task, double delayInSeconds) override
            {
                PostDelayed(std::move(task), delayInSeconds);
            }

            void PostNonNestableDelayedTask(std::unique_ptr<v8::Task> task, double delayInSeconds) override
            {
                PostDelayed(std::move(task), delayInSeconds);
            }

            void PostIdleTask(std::unique_ptr<v8::IdleTask>) override
            {
            }

            bool IdleTasksEnabled() override { return false; }
            bool NonNestableTasksEnabled() const override { return true; }
            bool NonNestableDelayedTasksEnabled() const override { return true; }

        private:
            void PostImmediate(std::unique_ptr<v8::Task> task)
            {
                auto host = m_platform.GetHost(m_isolate);
                if (host.runtime == nullptr)
                {
                    return;
                }

                auto shared = std::shared_ptr<v8::Task>(std::move(task));
                host.runtime->Dispatch([shared, isolate = m_isolate](Napi::Env) {
                    v8::Isolate::Scope isolate_scope{isolate};
                    shared->Run();
                });
            }

            void PostDelayed(std::unique_ptr<v8::Task> task, double delayInSeconds)
            {
                auto host = m_platform.GetHost(m_isolate);
                if (host.runtime == nullptr || host.scheduler == nullptr)
                {
                    return;
                }

                auto shared = std::shared_ptr<v8::Task>(std::move(task));
                auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(delayInSeconds));
                if (delay.count() < 0)
                {
                    delay = std::chrono::milliseconds{0};
                }

                std::scoped_lock lock{m_mutex};
                const auto id = host.scheduler->Schedule(delay, [runtime = host.runtime, isolate = m_isolate, shared]() {
                    runtime->Dispatch([shared, isolate](Napi::Env) {
                        v8::Isolate::Scope isolate_scope{isolate};
                        shared->Run();
                    });
                });
                m_pending.push_back(Pending{host.scheduler, id});
            }

            struct Pending
            {
                DeadlineScheduler* scheduler{};
                DeadlineScheduler::Id id{};
            };

            DispatchingPlatform& m_platform;
            v8::Isolate* m_isolate;
            std::mutex m_mutex;
            std::vector<Pending> m_pending;
        };

        std::shared_ptr<v8::TaskRunner> DispatchingPlatform::WrapForegroundTaskRunner(v8::Isolate* isolate)
        {
            std::scoped_lock lock{m_mutex};
            auto& runner = m_taskRunners[isolate];
            if (!runner)
            {
                runner = std::make_shared<DispatchingTaskRunner>(*this, isolate);
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

            DispatchingPlatform& Platform()
            {
                return *m_platform;
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
        Module::Instance().Platform().SetCurrentHost(this, &GetDeadlineScheduler());

        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        v8::Isolate* isolate = v8::Isolate::New(create_params);

        Module::Instance().Platform().SetHost(isolate, this, &GetDeadlineScheduler());

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
        Module::Instance().Platform().SetHost(isolate, nullptr, nullptr);
        Module::Instance().Platform().SetCurrentHost(nullptr, nullptr);

        // todo : GetArrayBufferAllocator not available?
        // delete isolate->GetArrayBufferAllocator();
        isolate->Dispose();
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // V8 auto-drains microtasks at the end of each script/callback when using the default
        // MicrotasksPolicy. Foreground platform tasks (including async WebAssembly settlement)
        // are posted through DispatchingTaskRunner onto AppRuntime's dispatcher, so they do
        // not need a PumpMessageLoop here.
    }
}
