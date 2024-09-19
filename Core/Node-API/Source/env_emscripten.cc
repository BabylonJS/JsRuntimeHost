#include <emscripten.h>
#include <napi/env.h>
#include <napi/js_native_api_types.h>

namespace Napi {
Napi::HandleScope *global_scope = nullptr;
Napi::Env Attach() {
  EM_ASM({
    try {
      Module.emnapiInit({context : emnapi.getDefaultContext()});
    } catch (err) {
      console.error(err);
      return;
    }
  });

  Napi::Env env{(napi_env)1};
  global_scope = new Napi::HandleScope(env);
  return {env};
}

void Detach(Napi::Env env) { delete global_scope; }

void GetContext(Napi::Env env) { return; }
} // namespace Napi

extern "C" {
EMSCRIPTEN_KEEPALIVE

napi_value napi_register_wasm_v1(napi_env env, napi_value exports) {
  return exports;
}

int32_t node_api_module_get_api_version_v1(void) { return 1; }
}
