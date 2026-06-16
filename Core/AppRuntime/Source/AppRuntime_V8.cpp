#include "AppRuntime.h"
#include <napi/env.h>

#include <libplatform/libplatform.h>

#ifdef ENABLE_V8_INSPECTOR
#include <V8InspectorAgent.h>
#endif

#include <optional>
#include <unordered_map>
#include <utility>

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

        // Tracks promises rejected without a handler so they can be reported once the current turn
        // settles. V8 fires kPromiseRejectWithNoHandler when a promise is rejected with no handler
        // and kPromiseHandlerAddedAfterReject if one is attached afterwards; reporting is deferred
        // (via AppRuntime::Dispatch) so a synchronous `Promise.reject(e); ...; p.catch(...)` is
        // removed before it is ever reported. Keyed by promise identity hash; the promise and reason
        // are held in v8::Global handles so they survive until the deferred flush runs.
        struct V8RejectionTracker
        {
            AppRuntime* runtime{};
            v8::Isolate* isolate{};
            std::unordered_map<int, std::pair<v8::Global<v8::Promise>, v8::Global<v8::Value>>> unhandled{};
            bool flushScheduled{false};
        };

        // The promise-rejection callback is a bare function pointer with no user-data argument. Each
        // AppRuntime owns a dedicated isolate running on its own thread, and V8 invokes the callback
        // on that thread, so a thread_local pointer associates the callback with the right tracker
        // without risking isolate-data-slot collisions with the Node-API shim.
        thread_local V8RejectionTracker* t_rejectionTracker{nullptr};

        // Mirrors v8impl::JsValueFromV8LocalValue (js_native_api_v8.h), which is internal to the
        // Node-API V8 shim and not on the public include path.
        static_assert(sizeof(v8::Local<v8::Value>) == sizeof(napi_value),
            "Cannot convert between v8::Local<v8::Value> and napi_value");
        napi_value JsValueFromV8LocalValue(v8::Local<v8::Value> local)
        {
            return reinterpret_cast<napi_value>(*local);
        }

        // Wrap a rejection reason as a Napi::Error. An Error-like object is forwarded as-is
        // (preserving message/stack/cause); any other value is stringified so the embedder's handler
        // always receives a Napi::Error. Done here (not in shared code) because the napi_value ->
        // Napi::Value bridge is specific to the V8/standard Node-API shim.
        Napi::Error ToError(Napi::Env env, napi_value reason)
        {
            const Napi::Value reasonValue{env, reason};
            return reasonValue.IsObject()
                ? Napi::Error{env, reason}
                : Napi::Error::New(env, reasonValue.ToString().Utf8Value());
        }

        void FlushUnhandledRejections(V8RejectionTracker& tracker, Napi::Env env)
        {
            tracker.flushScheduled = false;

            v8::Isolate::Scope isolateScope{tracker.isolate};
            v8::HandleScope handleScope{tracker.isolate};
            for (auto& entry : tracker.unhandled)
            {
                const v8::Local<v8::Value> reason = entry.second.second.Get(tracker.isolate);
                tracker.runtime->OnUnhandledPromiseRejection(ToError(env, JsValueFromV8LocalValue(reason)));
            }
            tracker.unhandled.clear();
        }

        void OnPromiseReject(v8::PromiseRejectMessage message)
        {
            V8RejectionTracker* tracker{t_rejectionTracker};
            if (tracker == nullptr)
            {
                return;
            }

            v8::HandleScope handleScope{tracker->isolate};
            const v8::Local<v8::Promise> promise{message.GetPromise()};
            const int hash{promise->GetIdentityHash()};

            switch (message.GetEvent())
            {
                case v8::kPromiseRejectWithNoHandler:
                {
                    const v8::Local<v8::Value> reason{message.GetValue()};
                    tracker->unhandled[hash] = {
                        v8::Global<v8::Promise>{tracker->isolate, promise},
                        v8::Global<v8::Value>{tracker->isolate, reason}};

                    if (!tracker->flushScheduled)
                    {
                        tracker->flushScheduled = true;
                        tracker->runtime->Dispatch([tracker](Napi::Env env) {
                            FlushUnhandledRejections(*tracker, env);
                        });
                    }
                    break;
                }
                case v8::kPromiseHandlerAddedAfterReject:
                {
                    tracker->unhandled.erase(hash);
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

            V8RejectionTracker rejectionTracker{this, isolate, {}, false};
            if (m_options.EnableUnhandledPromiseRejectionTracking)
            {
                t_rejectionTracker = &rejectionTracker;
                isolate->SetPromiseRejectCallback(OnPromiseReject);
            }

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

            if (m_options.EnableUnhandledPromiseRejectionTracking)
            {
                isolate->SetPromiseRejectCallback(nullptr);
                t_rejectionTracker = nullptr;
            }

            Napi::Detach(env);
        }

        // Destroy the isolate.
        // todo : GetArrayBufferAllocator not available?
        // delete isolate->GetArrayBufferAllocator();
        isolate->Dispose();
    }
}
