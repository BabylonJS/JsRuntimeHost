#include <napi/env.h>
#include "js_native_api_quickjs.h"
#include <quickjs.h>

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
            JSValue prototype = JS_GetPrototype(context, object);
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
            // We only release the JS side here. The RefInfo allocations
            // themselves are owned by their C++ holders (e.g. an
            // Napi::FunctionReference embedded in a polyfill object) and
            // will be freed later when those holders are destroyed. We
            // zero-out count/value so the subsequent napi_delete_reference
            // is a no-op and does not touch the already freed context.
            for (void* p : env_ptr->refs_list)
            {
                auto* info = reinterpret_cast<RefInfo*>(p);
                if (info->count > 0)
                {
                    JS_FreeValue(env_ptr->context, info->value);
                }
                info->count = 0;
                info->value = JS_UNDEFINED;
            }
            env_ptr->refs_list.clear();
            env_ptr->detached = true;

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

            // NOTE: we intentionally do NOT `delete env_ptr` here. Some
            // native objects (e.g. ObjectWrap instances) have their
            // destructors run as a side effect of JS_FreeContext's
            // teardown cascade (via napi_wrap finalizers) which happens
            // *after* Detach returns. Those destructors reach
            // napi_delete_reference on an env pointer that must remain
            // valid. The env is leaked, but this is bounded (one per
            // AppRuntime environment tier).
        }
    }

    JSContext* GetContext(Env env)
    {
        napi_env env_ptr{env};
        return env_ptr->context;
    }
}
