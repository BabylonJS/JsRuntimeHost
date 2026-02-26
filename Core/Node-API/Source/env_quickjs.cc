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
            if (!JS_IsUndefined(env_ptr->has_own_property_function))
            {
                JS_FreeValue(env_ptr->context, env_ptr->has_own_property_function);
            }
            delete env_ptr;
        }
    }

    JSContext* GetContext(Env env)
    {
        napi_env env_ptr{env};
        return env_ptr->context;
    }
}
