#pragma once

#include <napi/napi.h>

#define JSRUNTIMEHOST_PLATFORM "EMSCRIPTEN"

namespace Napi {
Napi::Env Attach();

void Detach(Napi::Env);

Napi::Value Eval(Napi::Env env, const char *source, const char *sourceUrl);
}; // namespace Napi
