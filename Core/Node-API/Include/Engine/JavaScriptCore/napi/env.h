#pragma once

#include <napi/env_shared.h>
#include <JavaScriptCore/JavaScript.h>

namespace Napi
{
  Napi::Env Attach(JSGlobalContextRef);

  JSGlobalContextRef GetContext(Napi::Env);
}
