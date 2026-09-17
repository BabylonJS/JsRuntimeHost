#include <napi/env.h>

#include <utility>

namespace Napi
{
  Env Attach(facebook::jsi::Runtime& rt)
  {
    napi_env__* env_ptr{new napi_env__{rt}};
    return {env_ptr};
  }

  void Detach(Env env)
  {
    napi_env__* env_ptr{env};
    delete env_ptr;
  }

  Napi::Value Eval(Napi::Env env, const char* string, const char* sourceUrl)
  {
    napi_env__* env_ptr{env};
    try
    {
      return {env_ptr, env_ptr->rt.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(string), sourceUrl)};
    }
    catch (const facebook::jsi::JSError& error)
    {
      // Napi::Error is object-backed in this JSI implementation. Preserve thrown objects exactly;
      // represent primitive throws with a new Error rather than calling asObject and leaking a
      // second JSIException into AppRuntime's fatal catch-all.
      auto value = facebook::jsi::Value{env_ptr->rt, error.value()};
      if (value.isObject())
      {
        throw Napi::Error{env_ptr, std::move(value)};
      }
      throw Napi::Error::New(env, error.what());
    }
    catch (const facebook::jsi::JSIException& error)
    {
      throw Napi::Error::New(env, error.what());
    }
  }
}
