#include <napi/env.h>
#include "js_native_api_quickjs.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <quickjs.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace Napi
{
    Env Attach(JSContext* context)
    {
        napi_env env_ptr{new napi_env__};
        env_ptr->context = context;
        env_ptr->current_context = env_ptr->context; 

        // Get Object.prototype.hasOwnProperty
        JSValue global = JS_GetGlobalObject(context);
        JSValue object = JS_GetPropertyStr(context, global, "Object");
        if (!JS_IsException(object) && JS_IsObject(object))
        {
            // Object.prototype.hasOwnProperty. Note: use the constructor's
            // "prototype" property to get Object.prototype. JS_GetPrototype(object)
            // would return the Object *constructor's* [[Prototype]]
            // (Function.prototype), from which hasOwnProperty is only reachable by
            // inheritance - correct by luck, but not by intent.
            JSValue prototype = JS_GetPropertyStr(context, object, "prototype");
            if (!JS_IsException(prototype) && JS_IsObject(prototype))
            {
                JSValue hasOwnProperty = JS_GetPropertyStr(context, prototype, "hasOwnProperty");
                if (!JS_IsException(hasOwnProperty))
                {
                    env_ptr->has_own_property_function = hasOwnProperty;
                }
                else
                {
                    JS_FreeValue(context, hasOwnProperty);
                }
                JS_FreeValue(context, prototype);
            }
            else if (JS_IsException(prototype))
            {
                JS_FreeValue(context, prototype);
            }
            JS_FreeValue(context, object);
        }
        else if (JS_IsException(object))
        {
            JS_FreeValue(context, object);
        }
        JS_FreeValue(context, global);

        return {env_ptr};
    }

    void Detach(Env env)
    {
        napi_env env_ptr{env};
        if (env_ptr)
        {
            // Release every strong napi_ref still outstanding. This mirrors
            // the V8 impl (napi_env__::DeleteMe) and is essential on QuickJS:
            // any surviving strong ref pins a JS value from outside the GC
            // graph, which prevents the teardown cascade in JS_FreeContext
            // from running and triggers list_empty(gc_obj_list) assert in
            // JS_FreeRuntime.
            //
            // Freeing a value can synchronously run a napi_wrap finalizer
            // whose C++ destructor releases *other* embedded napi_refs (e.g.
            // an AbortController destroying its AbortSignal ObjectReference).
            // Those nested napi_delete_reference calls must not perform a real
            // JS_FreeValue - otherwise a value can be freed twice - and must
            // not mutate refs_list while we iterate it. So we first neutralize
            // every ref (count/value zeroed, list cleared) and only then free
            // the snapshotted values. Any finalizer-driven
            // napi_delete_reference then sees count == 0 and is a safe no-op.
            std::vector<JSValue> strongValues;
            strongValues.reserve(env_ptr->refs_list.size());
            for (void* p : env_ptr->refs_list)
            {
                auto* info = reinterpret_cast<RefInfo*>(p);
                if (info->count > 0)
                {
                    strongValues.push_back(info->value);
                }
                info->count = 0;
                info->value = JS_UNDEFINED;
            }
            env_ptr->refs_list.clear();
            env_ptr->detached = true;

            for (JSValue value : strongValues)
            {
                JS_FreeValue(env_ptr->context, value);
            }

            if (!JS_IsUndefined(env_ptr->has_own_property_function))
            {
                JS_FreeValue(env_ptr->context, env_ptr->has_own_property_function);
                env_ptr->has_own_property_function = JS_UNDEFINED;
            }

            // Free all remaining JSValues in the handle scope stack
            for (auto& ptr : env_ptr->handle_scope_stack)
            {
                JS_FreeValue(env_ptr->context, *ptr);
            }
            env_ptr->handle_scope_stack.clear();

            // Run the cycle collector so napi_wrap finalizers (which
            // destroy C++ wrapper objects and release any embedded
            // napi_refs) get a chance to execute while the env is still
            // valid. A second pass picks up anything unpinned by the
            // first pass's finalizers.
            JSRuntime* rt = JS_GetRuntime(env_ptr->context);
            JS_RunGC(rt);
            JS_RunGC(rt);

            // Drop the initial owner reference taken in Attach. If every
            // ExternalData finalizer already ran during the GC passes above,
            // this deletes the env now. If some are deferred to the engine's
            // JS_FreeContext/JS_FreeRuntime teardown cascade (the common case),
            // each still holds a count, so the env survives until the last such
            // finalizer completes and drops the final count - deleting the env
            // exactly once, after its last use, with no leak.
            //
            // NOTE: env_ptr must not be touched after this point; it may have
            // already been deleted.
            env_ptr->Unref();
        }
    }

    JSContext* GetContext(Env env)
    {
        napi_env env_ptr{env};
        return env_ptr->context;
    }
}
