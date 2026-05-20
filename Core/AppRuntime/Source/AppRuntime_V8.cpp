#include "AppRuntime.h"
#include <napi/env.h>

#include <libplatform/libplatform.h>

#ifdef ENABLE_V8_INSPECTOR
#include <V8InspectorAgent.h>
#endif

#include <optional>

namespace Babylon
{
    namespace internal
    {
        // Defined in AppRuntime.cpp; the engine-agnostic dispatcher loop
        // calls this between ticks so we can pump V8-side foreground tasks.
        void SetPostTickHook(std::function<void()> hook);
    }

    namespace
    {
        class Module final
        {
        public:
            Module(const char* executablePath)
            {
                v8::V8::InitializeICUDefaultLocation(executablePath);
                v8::V8::InitializeExternalStartupData(executablePath);
                m_platform = v8::platform::NewDefaultPlatform();
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

            v8::Platform& Platform()
            {
                return *m_platform;
            }

        private:
            std::unique_ptr<v8::Platform> m_platform;

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

        // Use the isolate within a scope.
        {
            v8::Isolate::Scope isolate_scope{isolate};
            v8::HandleScope isolate_handle_scope{isolate};
            v8::Local<v8::Context> context = v8::Context::New(isolate);
            v8::Context::Scope context_scope{context};

            Napi::Env env = Napi::Attach(context);

            // Install the post-tick hook so the dispatcher loop drains V8's
            // foreground task runner (which holds async WebAssembly compile
            // continuations and other deferred Platform-scheduled work)
            // and any pending microtasks after each AppRuntime dispatch.
            // Without this, async WASM-using code (Draco glTF extension,
            // Basis, KTX2, ...) freezes because its resolving Promise
            // continuation is never invoked.
            v8::Platform* platformPtr = &Module::Instance().Platform();
            internal::SetPostTickHook([platformPtr, isolate]() {
                while (v8::platform::PumpMessageLoop(
                    platformPtr, isolate,
                    v8::platform::MessageLoopBehavior::kDoNotWait)) {}
                isolate->PerformMicrotaskCheckpoint();
            });

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

            // Drop the hook before the isolate (captured by reference)
            // goes out of scope; otherwise a stale capture could fire
            // during teardown.
            internal::SetPostTickHook(nullptr);
        }

        // Destroy the isolate.
        // todo : GetArrayBufferAllocator not available?
        // delete isolate->GetArrayBufferAllocator();
        isolate->Dispose();
    }
}
