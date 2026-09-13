// pthread_getattr_np is declared by glibc only under the GNU feature set; gnu++20 predefines it,
// but this translation unit should not depend on the language dialect for a system declaration.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "AppRuntime.h"
#include <napi/env.h>

#ifdef _WIN32
#pragma warning(push)
// cast from int64 to int32
#pragma warning(disable : 4244)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <quickjs.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#ifdef _WIN32
#pragma warning(pop)
#endif

#if !defined(_WIN32)
#include <pthread.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Babylon
{
    namespace
    {
        // QuickJS guards against JS recursion by comparing the C stack pointer against
        // stack_top - stack_size, where stack_size defaults to JS_DEFAULT_STACK_SIZE (1 MiB in
        // quickjs-ng). That is also the default size of a non-main thread on Android and Windows,
        // so on those threads the limit sits below the real guard page: deep recursion faults
        // (SIGSEGV) before QuickJS can raise "InternalError: stack overflow". Derive the limit
        // from the thread that actually runs the runtime instead, keeping a margin for the native
        // frames QuickJS and the host add between the check and the guard page.
        size_t JavaScriptStackLimit()
        {
            constexpr size_t Margin{256 * 1024};
            constexpr size_t Fallback{256 * 1024};
            size_t threadStack{};
#if defined(_WIN32)
            ULONG_PTR low{};
            ULONG_PTR high{};
            GetCurrentThreadStackLimits(&low, &high);
            threadStack = static_cast<size_t>(high - low);
#elif defined(__APPLE__)
            // pthread_getattr_np is a GNU/bionic extension; Apple exposes the size directly.
            threadStack = pthread_get_stacksize_np(pthread_self());
#else
            pthread_attr_t attributes;
            if (pthread_getattr_np(pthread_self(), &attributes) == 0)
            {
                void* base{};
                size_t size{};
                if (pthread_attr_getstack(&attributes, &base, &size) == 0)
                {
                    threadStack = size;
                }
                pthread_attr_destroy(&attributes);
            }
#endif
            if (threadStack == 0)
            {
                return Fallback; // unknown: conservative, well under any plausible thread
            }
            if (threadStack <= 2 * Margin)
            {
                return threadStack / 2; // a known small stack must not get a limit larger than itself
            }
            return std::min(threadStack - Margin, static_cast<size_t>(JS_DEFAULT_STACK_SIZE) * 8);
        }
    }

    void AppRuntime::RunEnvironmentTier(const char* /*executablePath*/)
    {
        // Create the runtime.
        JSRuntime* runtime = JS_NewRuntime();
        if (!runtime)
        {
            throw std::runtime_error{"Failed to create QuickJS runtime"};
        }
        JS_SetMaxStackSize(runtime, JavaScriptStackLimit());

        // Create the context.
        JSContext* context = JS_NewContext(runtime);
        if (!context)
        {
            JS_FreeRuntime(runtime);
            throw std::runtime_error{"Failed to create QuickJS context"};
        }

        // Use the context within a scope.
        {
            Napi::Env env = Napi::Attach(context);

            Run(env);

            Napi::Detach(env);
        }

        // Destroy the context and runtime.
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
    }

    void AppRuntime::ShutdownEnvironment(Napi::Env)
    {
    }

    void AppRuntime::DrainMicrotasks(Napi::Env env)
    {
        // QuickJS does not auto-drain its job queue. Promise continuations,
        // queueMicrotask callbacks, etc. are queued as "pending jobs" and only
        // run when explicitly pumped. We drain them here, after each user
        // callback, so async code observes the same "between turns" semantics
        // it gets on the auto-draining engines (V8/Chakra/JSC).
        JSRuntime* runtime = JS_GetRuntime(Napi::GetContext(env));
        JSContext* pending_ctx;
        while (JS_ExecutePendingJob(runtime, &pending_ctx) > 0)
        {
        }
    }
}
