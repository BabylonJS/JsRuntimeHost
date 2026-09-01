#include "AppRuntime.h"
#include "AppRuntime_PromiseRejection.h"
#include <napi/env.h>

#include <libplatform/libplatform.h>

#ifdef ENABLE_V8_INSPECTOR
#include <V8InspectorAgent.h>
#endif

#include <optional>

namespace Babylon
{
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

        // Mirrors v8impl::JsValueFromV8LocalValue (js_native_api_v8.h), which is internal to the
        // Node-API V8 shim and not on the public include path.
        static_assert(sizeof(v8::Local<v8::Value>) == sizeof(napi_value),
            "Cannot convert between v8::Local<v8::Value> and napi_value");
        napi_value JsValueFromV8LocalValue(v8::Local<v8::Value> local)
        {
            return reinterpret_cast<napi_value>(*local);
        }

        // A promise rejected without a handler, awaiting end-of-turn reporting. The promise and
        // reason are held in v8::Global handles so they survive until the deferred flush; the promise
        // is retained so a later handler-added event can drop this candidate by object identity
        // (v8::Object::GetIdentityHash is not unique, so identity comparison is used instead).
        struct V8RejectionCandidate
        {
            v8::Isolate* isolate{};
            v8::Global<v8::Promise> promise;
            v8::Global<v8::Value> reason;

            void Report(AppRuntime& runtime, Napi::Env env) const
            {
                v8::HandleScope handleScope{isolate};
                runtime.OnUnhandledPromiseRejection(Internal::ToError(env, JsValueFromV8LocalValue(reason.Get(isolate))));
            }
        };

        using V8RejectionTracker = Internal::PromiseRejectionTracker<V8RejectionCandidate>;

        // The promise-rejection callback is a bare function pointer with no user-data argument. Each
        // AppRuntime owns a dedicated isolate running on its own thread, and V8 invokes the callback
        // on that thread, so a thread_local pointer associates the callback with the right tracker
        // without risking isolate-data-slot collisions with the Node-API shim.
        thread_local V8RejectionTracker* t_rejectionTracker{nullptr};

        void OnPromiseReject(v8::PromiseRejectMessage message)
        {
            V8RejectionTracker* tracker{t_rejectionTracker};
            if (tracker == nullptr)
            {
                return;
            }

            v8::Isolate* isolate{v8::Isolate::GetCurrent()};
            v8::HandleScope handleScope{isolate};
            const v8::Local<v8::Promise> promise{message.GetPromise()};

            switch (message.GetEvent())
            {
                case v8::kPromiseRejectWithNoHandler:
                {
                    tracker->Add(V8RejectionCandidate{
                        isolate,
                        v8::Global<v8::Promise>{isolate, promise},
                        v8::Global<v8::Value>{isolate, message.GetValue()}});
                    break;
                }
                case v8::kPromiseHandlerAddedAfterReject:
                {
                    tracker->Remove([isolate, promise](const V8RejectionCandidate& candidate) {
                        return candidate.promise.Get(isolate) == promise;
                    });
                    break;
                }
                default:
                {
                    // kPromiseRejectAfterResolved / kPromiseResolveAfterResolved carry no actionable
                    // unhandled-rejection signal.
                    break;
                }
            }
        }
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

            // Always track unhandled promise rejections (routed to the host UnhandledExceptionHandler).
            V8RejectionTracker rejectionTracker{*this};
            t_rejectionTracker = &rejectionTracker;
            isolate->SetPromiseRejectCallback(OnPromiseReject);

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

            isolate->SetPromiseRejectCallback(nullptr);
            t_rejectionTracker = nullptr;

            Napi::Detach(env);
        }

        // Destroy the isolate.
        // todo : GetArrayBufferAllocator not available?
        // delete isolate->GetArrayBufferAllocator();
        isolate->Dispose();
    }

    void AppRuntime::DrainMicrotasks(Napi::Env)
    {
        // V8 auto-drains microtasks at the end of each script/callback when
        // using the default MicrotasksPolicy.  No explicit pump needed.
    }
}
