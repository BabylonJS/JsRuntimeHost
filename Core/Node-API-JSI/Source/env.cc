#include <napi/env.h>

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

  Napi::Value Eval(Napi::Env env, const char* source, const char* sourceUrl)
  {
    return env.RunScript(source, sourceUrl);
  }
}
