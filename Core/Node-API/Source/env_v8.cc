#include <napi/env.h>
#include <napi/js_native_api_types.h>
#include "js_native_api_v8.h"

namespace Napi
{
  Env Attach(v8::Local<v8::Context> isolate)
  {
    // second argument is module version
    return {new napi_env__(isolate, NAPI_VERSION)};
  }

  void Detach(Env env)
  {
    napi_env env_ptr{env};
    env_ptr->DeleteMe();
  }

  void DrainFinalizers(Env env)
  {
    napi_env env_ptr{env};

    // Finalizers can mutate this queue, so continue until it is empty.
    while (!env_ptr->pending_finalizers.empty())
    {
      auto* finalizer = *env_ptr->pending_finalizers.begin();
      env_ptr->pending_finalizers.erase(finalizer);
      finalizer->Finalize();
    }
  }

  v8::Local<v8::Context> GetContext(Env env)
  {
    napi_env env_ptr{env};
    return env_ptr->context();
  }
}
