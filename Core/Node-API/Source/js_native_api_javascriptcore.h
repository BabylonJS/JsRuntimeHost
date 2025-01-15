#pragma once

#include <napi/js_native_api.h>
#include <napi/js_native_api_types.h>
#include <JavaScriptCore/JavaScript.h>
#include <unordered_map>
#include <list>
#include <thread>
#include <cassert>

struct napi_env__ {
  JSGlobalContextRef context{};
  JSValueRef last_exception{};
  napi_extended_error_info last_error{nullptr, nullptr, 0, napi_ok};
  std::unordered_map<napi_value, std::uintptr_t> active_ref_values{};
  std::list<napi_ref> strong_refs{};

  JSValueRef reference_info_symbol{};
  JSValueRef wrapper_info_symbol{};

  const std::thread::id thread_id{std::this_thread::get_id()};

  napi_env__(JSGlobalContextRef context) : context{context} {
    JSGlobalContextRetain(context);
    init_symbol(reference_info_symbol, "BabylonNative_ReferenceInfo");
    init_symbol(wrapper_info_symbol, "BabylonNative_WrapperInfo");
  }

  ~napi_env__() {
    deinit_refs();
    deinit_symbol(wrapper_info_symbol);
    deinit_symbol(reference_info_symbol);
    JSGlobalContextRelease(context);
  }

 private:
  void deinit_refs();

  void init_symbol(JSValueRef& symbol, const char* description) {
    JSStringRef descriptionRef{JSStringCreateWithUTF8CString(description)};
    symbol = JSValueMakeSymbol(context, descriptionRef);
    JSValueProtect(context, symbol);
  }

  void deinit_symbol(JSValueRef symbol) {
    JSValueUnprotect(context, symbol);
  }
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
