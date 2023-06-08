#pragma once

#include <napi/env_shared.h>
#include <v8.h>

namespace Napi
{
  Napi::Env Attach(v8::Local<v8::Context>);

  v8::Local<v8::Context> GetContext(Napi::Env);
}
