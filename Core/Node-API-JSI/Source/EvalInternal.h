#pragma once

#include <napi/env.h>
#include <utility>

namespace Napi::Internal
{
    inline Error ConvertEvalException(Env env, const facebook::jsi::JSError& error)
    {
        // Napi::Error is object-backed here. Preserve objects supplied by JSError;
        // wrap primitives instead of letting asObject throw another JSIException.
        napi_env__* env_ptr{env};
        auto value = facebook::jsi::Value{env_ptr->rt, error.value()};
        if (value.isObject())
        {
            return Error{env_ptr, std::move(value)};
        }
        return Error::New(env, error.what());
    }

    inline Error ConvertEvalException(Env env, const facebook::jsi::JSIException& error)
    {
        return Error::New(env, error.what());
    }
}
