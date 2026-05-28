#include "AppRuntime.h"
#include <napi/env.h>

#define USE_EDGEMODE_JSRT
#include <jsrt.h>

#include <gsl/gsl>
#include <cassert>
#include <sstream>

namespace Babylon
{
    namespace internal
    {
        // Defined in AppRuntime.cpp; the engine-agnostic dispatcher loop
        // calls this between ticks so we can pump engine-managed task
        // queues that are not part of the AppRuntime dispatcher.
        void SetPostTickHook(std::function<void()> hook);
    }

    namespace
    {
        void ThrowIfFailed(JsErrorCode errorCode)
        {
            if (errorCode != JsErrorCode::JsNoError)
            {
                std::ostringstream ss;
                ss << "Chakra function failed with error code (" << static_cast<int>(errorCode) << ")";
                throw std::runtime_error{ss.str()};
            }
        }
    }

    void AppRuntime::RunEnvironmentTier(const char*)
    {
        using DispatchFunction = std::function<void(std::function<void()>)>;
        DispatchFunction dispatchFunction{
            [this](std::function<void()> action) {
                Dispatch([action = std::move(action)](Napi::Env) {
                    action();
                });
            }};

        JsRuntimeHandle jsRuntime;
        ThrowIfFailed(JsCreateRuntime(JsRuntimeAttributeNone, nullptr, &jsRuntime));
        JsContextRef context;
        ThrowIfFailed(JsCreateContext(jsRuntime, &context));
        ThrowIfFailed(JsSetCurrentContext(context));

        // Chakra/EdgeJsRt route promise microtasks (Promise.then jobs,
        // await continuations) through this callback. Each job is wrapped
        // in a Dispatch onto the AppRuntime queue, where it runs on the
        // next tick.
        //
        // ASYNC WASM NOTE: ChakraCore's WebAssembly.compile / .instantiate
        // do not use background worker threads in the public JSRT API --
        // the compile runs synchronously within the Promise's resolve
        // step, which itself fires through this PromiseContinuationCallback.
        // No separate platform-task pump is required for async WASM here,
        // unlike V8.
        ThrowIfFailed(JsSetPromiseContinuationCallback(
            [](JsValueRef task, void* callbackState) {
                ThrowIfFailed(JsAddRef(task, nullptr));
                auto* dispatch = reinterpret_cast<DispatchFunction*>(callbackState);
                dispatch->operator()([task]() {
                    JsValueRef undefined;
                    ThrowIfFailed(JsGetUndefinedValue(&undefined));
                    ThrowIfFailed(JsCallFunction(task, &undefined, 1, nullptr));
                    ThrowIfFailed(JsRelease(task, nullptr));
                });
            },
            &dispatchFunction));
        ThrowIfFailed(JsProjectWinRTNamespace(L"Windows"));

        if (m_options.EnableDebugger)
        {
            auto result = JsStartDebugging();
            if (result != JsErrorCode::JsNoError)
            {
                OutputDebugStringW(L"Failed to initialize JavaScript debugging support.\n");
            }
        }

        Napi::Env env = Napi::Attach();

        // No post-tick hook needed for Chakra: there is no embedder-owned
        // platform task queue analogous to V8's foreground task runner,
        // and ChakraCore's WASM completion is delivered through the
        // promise-continuation callback above. If a future host needs to
        // drain a Chakra-managed work queue here, install via
        // internal::SetPostTickHook.

        Run(env);

        ThrowIfFailed(JsSetCurrentContext(JS_INVALID_REFERENCE));
        ThrowIfFailed(JsDisposeRuntime(jsRuntime));

        // Detach must come after JsDisposeRuntime since it triggers finalizers which require env.
        Napi::Detach(env);
    }
}
