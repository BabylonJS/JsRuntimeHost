#include "AppRuntime.h"

#include <napi/env.h>
#include <V8JsiRuntime.h>
#include <ScriptStore.h>

namespace
{
    // ASYNC WASM NOTE: V8JSI exposes V8's foreground task runner as an
    // injectable JSITaskRunner. V8 schedules asynchronous work --
    // including WebAssembly.compile / .instantiate completion callbacks
    // posted from the WASM compile worker threads -- via postTask on
    // this runner.
    //
    // TaskRunnerAdapter routes each posted task into the AppRuntime
    // dispatcher (m_runtime.Dispatch), where it will run on the next
    // tick of the dispatcher loop. Because each tick also drains JS
    // microtasks (V8JSI auto-pumps microtasks at the end of every JS
    // entry it makes for us), the resolving Promise's continuation runs
    // immediately after the WASM completion task fires.
    //
    // This is the moral equivalent of the explicit
    // PumpMessageLoop + PerformMicrotaskCheckpoint hook installed by the
    // plain-V8 AppRuntime_V8.cpp -- here V8JSI owns the platform and we
    // bridge its foreground task runner directly. No additional
    // post-tick hook is required for async WASM to settle on V8JSI.
    class TaskRunnerAdapter : public v8runtime::JSITaskRunner
    {
    public:
        TaskRunnerAdapter(Babylon::AppRuntime& runtime)
            : m_runtime(runtime)
        {
        }

        void postTask(std::unique_ptr<v8runtime::JSITask> task) override
        {
            std::shared_ptr<v8runtime::JSITask> shared_task(std::move(task));
            m_runtime.Dispatch([shared_task2 = std::move(shared_task)](Napi::Env) {
                shared_task2->run();
            });
        }

    private:
        TaskRunnerAdapter(const TaskRunnerAdapter&) = delete;
        TaskRunnerAdapter& operator=(const TaskRunnerAdapter&) = delete;

        Babylon::AppRuntime& m_runtime;
    };
}

namespace Babylon
{
    void AppRuntime::RunEnvironmentTier(const char*)
    {
        v8runtime::V8RuntimeArgs args{};
        args.inspectorPort = 5643;
        args.foreground_task_runner = std::make_shared<TaskRunnerAdapter>(*this);
        const auto runtime = v8runtime::makeV8Runtime(std::move(args));

        const auto env = Napi::Attach(*runtime);

        Run(env);

        Napi::Detach(env);
    }
}
