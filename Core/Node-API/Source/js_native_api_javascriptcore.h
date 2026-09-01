#pragma once

#include <napi/js_native_api.h>
#include <napi/js_native_api_types.h>
#include <JavaScriptCore/JavaScript.h>
#include <unordered_map>
#include <list>
#include <thread>
#include <cassert>
#include <map>

struct napi_env__ {
  JSGlobalContextRef context{};
  JSValueRef last_exception{};
  napi_extended_error_info last_error{nullptr, nullptr, 0, napi_ok};
  std::unordered_map<napi_value, std::uintptr_t> active_ref_values{};
  std::list<napi_ref> strong_refs{};
  bool shutting_down{false};

  // napi_set_instance_data / napi_get_instance_data (N-API v6).
  void* instance_data{};
  napi_finalize instance_data_finalize_cb{};
  void* instance_data_finalize_hint{};

  JSValueRef constructor_info_symbol{};
  JSValueRef function_info_symbol{};
  JSValueRef reference_info_symbol{};
  JSValueRef wrapper_info_symbol{};
  JSValueRef function_prototype_call{};

  // BigInt intrinsics, captured once at env init -- before any user script can run -- so the BigInt
  // entry points never resolve `BigInt`, `BigInt.asIntN/asUintN` or `BigInt.prototype.toString`
  // through the (monkey-patchable) global object on a live context. Same invariant as
  // function_prototype_call above.
  JSValueRef is_bigint_function{};        // (v) => typeof v === 'bigint'
  JSValueRef bigint_constructor{};        // BigInt
  JSValueRef bigint_as_int_n{};           // BigInt.asIntN
  JSValueRef bigint_as_uint_n{};          // BigInt.asUintN
  JSValueRef bigint_prototype_to_string{};// BigInt.prototype.toString
  JSValueRef bigint_negate{};             // (v) => -v
  bool bigint_supported{false};

  // ArrayBuffer detach intrinsics, captured at env init for the same reason. Both stay null on an
  // engine without ES2024 ArrayBuffer.prototype.transfer / .detached (every jsc-android build, and
  // Apple platforms below macOS 14.4 / iOS 17.4).
  JSValueRef arraybuffer_transfer{};       // ArrayBuffer.prototype.transfer
  JSValueRef arraybuffer_detached_getter{};// get ArrayBuffer.prototype.detached

  // Escapable scope bookkeeping: token -> whether that scope has escaped. Values
  // are rooted by the engine rather than by a scope here, so this exists only to
  // honour the one-escape-per-scope rule and to reject tokens that are not open.
  // The token is a monotonic counter, never an index into anything, so two scopes
  // can never share one.
  size_t next_escapable_scope_token{0};
  std::map<size_t, bool> open_escapable_scopes{};

  const std::thread::id thread_id{std::this_thread::get_id()};

  napi_env__(JSGlobalContextRef context) : context{context} {
    napi_envs[context] = this;
    JSGlobalContextRetain(context);
    init_symbol(constructor_info_symbol, "BabylonNative_ConstructorInfo");
    init_symbol(function_info_symbol, "BabylonNative_FunctionInfo");
    init_symbol(reference_info_symbol, "BabylonNative_ReferenceInfo");
    init_symbol(wrapper_info_symbol, "BabylonNative_WrapperInfo");
    init_function_prototype_call();
    init_is_bigint_function();
    init_bigint_intrinsics();
    init_arraybuffer_intrinsics();
  }

  ~napi_env__() {
    shutting_down = true;
    if (instance_data_finalize_cb != nullptr) {
      instance_data_finalize_cb(this, instance_data, instance_data_finalize_hint);
    }
    deinit_refs();
    deinit_symbol(arraybuffer_detached_getter);
    deinit_symbol(arraybuffer_transfer);
    deinit_symbol(bigint_negate);
    deinit_symbol(bigint_prototype_to_string);
    deinit_symbol(bigint_as_uint_n);
    deinit_symbol(bigint_as_int_n);
    deinit_symbol(bigint_constructor);
    deinit_symbol(is_bigint_function);
    deinit_symbol(function_prototype_call);
    deinit_symbol(wrapper_info_symbol);
    deinit_symbol(reference_info_symbol);
    deinit_symbol(function_info_symbol);
    deinit_symbol(constructor_info_symbol);
    JSGlobalContextRelease(context);
    napi_envs.erase(context);
  }

  static napi_env get(JSGlobalContextRef context) {
    auto it = napi_envs.find(context);
    if (it != napi_envs.end()) {
      return it->second;
    } else {
      return nullptr;
    }
  }

 private:
  static inline std::unordered_map<JSGlobalContextRef, napi_env> napi_envs{};

  void deinit_refs();
  void init_symbol(JSValueRef& symbol, const char* description);
  void init_function_prototype_call();
  void init_is_bigint_function();
  void init_bigint_intrinsics();
  void init_arraybuffer_intrinsics();
  void deinit_symbol(JSValueRef symbol);
};

#define RETURN_STATUS_IF_FALSE(env, condition, status) \
  do {                                                 \
    if (!(condition)) {                                \
      return napi_set_last_error((env), (status));     \
    }                                                  \
  } while (0)

#define CHECK_ENV(env)                                    \
  do {                                                    \
    if ((env) == nullptr) {                               \
      return napi_invalid_arg;                            \
    }                                                     \
    assert(env->thread_id == std::this_thread::get_id()); \
  } while (0)

#define CHECK_ARG(env, arg) \
  RETURN_STATUS_IF_FALSE((env), ((arg) != nullptr), napi_invalid_arg)

#define CHECK_JSC(env, exception)                \
  do {                                           \
    if ((exception) != nullptr) {                \
      return napi_set_exception(env, exception); \
    }                                            \
  } while (0)

// This does not call napi_set_last_error because the expression
// is assumed to be a NAPI function call that already did.
#define CHECK_NAPI(expr)                  \
  do {                                    \
    napi_status status = (expr);          \
    if (status != napi_ok) return status; \
  } while (0)
