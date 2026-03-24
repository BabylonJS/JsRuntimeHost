#pragma once

#include <napi/js_native_api.h>
#include <napi/js_native_api_types.h>
#include <jsi/jsi.h>
#include <hermes/hermes.h>
#include <deque>
#include <optional>
#include <vector>
#include <thread>
#include <cassert>
#include <functional>

struct napi_value__ {
  facebook::jsi::Value value;
  explicit napi_value__(facebook::jsi::Value&& v) : value(std::move(v)) {}
};

struct napi_env__ {
  facebook::jsi::Runtime& runtime;
  napi_extended_error_info last_error{nullptr, nullptr, 0, napi_ok};
  std::optional<facebook::jsi::Value> last_exception;
  std::deque<napi_value__> values;

  void* instance_data{nullptr};
  napi_finalize instance_data_finalize_cb{nullptr};
  void* instance_data_finalize_hint{nullptr};

  struct CleanupCallback {
    napi_finalize cb;
    void* data;
    void* hint;
  };
  std::vector<CleanupCallback> cleanup_callbacks;

  const std::thread::id thread_id{std::this_thread::get_id()};

  explicit napi_env__(facebook::jsi::Runtime& rt) : runtime{rt} {}

  ~napi_env__() {
    if (instance_data_finalize_cb) {
      instance_data_finalize_cb(this, instance_data, instance_data_finalize_hint);
    }
    for (auto& c : cleanup_callbacks) {
      if (c.cb) c.cb(this, c.data, c.hint);
    }
  }

  napi_value store_value(facebook::jsi::Value&& val) {
    values.emplace_back(std::move(val));
    return &values.back();
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

#define CHECK_NAPI(expr)                  \
  do {                                    \
    napi_status status = (expr);          \
    if (status != napi_ok) return status; \
  } while (0)
