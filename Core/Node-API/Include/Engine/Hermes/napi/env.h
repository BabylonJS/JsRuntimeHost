#pragma once

#include <napi/napi.h>
#include <hermes/hermes.h>

namespace Napi
{
  Napi::Env Attach(facebook::hermes::HermesRuntime& runtime);

  void Detach(Napi::Env);

  Napi::Value Eval(Napi::Env env, const char* source, const char* sourceUrl);

  facebook::hermes::HermesRuntime& GetRuntime(Napi::Env);
}
