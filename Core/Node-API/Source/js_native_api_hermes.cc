#include "js_native_api_hermes.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

namespace jsi = facebook::jsi;

// Helper to get a const reference to a jsi::Value from napi_value,
// ensuring the non-template toValue overload is selected.
inline const jsi::Value& V(napi_value v) { return v->value; }

struct napi_callback_info__ {
  napi_value newTarget;
  napi_value thisArg;
  napi_value* argv;
  void* data;
  uint16_t argc;
};

namespace {

napi_status napi_clear_last_error(napi_env env) {
  env->last_error.error_code = napi_ok;
  env->last_error.engine_error_code = 0;
  env->last_error.engine_reserved = nullptr;
  return napi_ok;
}

napi_status napi_set_last_error(napi_env env, napi_status error_code,
                                uint32_t engine_error_code = 0,
                                void* engine_reserved = nullptr) {
  env->last_error.error_code = error_code;
  env->last_error.engine_error_code = engine_error_code;
  env->last_error.engine_reserved = engine_reserved;
  return error_code;
}

napi_status napi_set_exception(napi_env env, const jsi::JSError& error) {
  try {
    env->last_exception = jsi::Value(env->runtime, error.value());
  } catch (...) {
    env->last_exception = jsi::Value::undefined();
  }
  return napi_set_last_error(env, napi_pending_exception);
}

napi_status napi_set_exception_value(napi_env env, jsi::Value&& val) {
  env->last_exception = std::move(val);
  return napi_set_last_error(env, napi_pending_exception);
}

napi_status napi_set_error_code(napi_env env, napi_value error,
                                napi_value code,
                                const char* code_cstring) {
  if (code != nullptr) {
    CHECK_NAPI(napi_set_named_property(env, error, "code", code));
  } else if (code_cstring != nullptr) {
    napi_value code_value{};
    CHECK_NAPI(napi_create_string_utf8(env, code_cstring, NAPI_AUTO_LENGTH,
                                       &code_value));
    CHECK_NAPI(napi_set_named_property(env, error, "code", code_value));
  }
  return napi_ok;
}

// HostObject for externals
class ExternalValue : public jsi::HostObject {
 public:
  napi_env env;
  void* data;
  napi_finalize release_cb;
  void* release_hint;

  ExternalValue(napi_env e, void* d, napi_finalize cb, void* hint)
      : env(e), data(d), release_cb(cb), release_hint(hint) {}
  ~ExternalValue() override {
    if (release_cb) release_cb(env, data, release_hint);
  }
  jsi::Value get(jsi::Runtime&, const jsi::PropNameID&) override {
    return jsi::Value::undefined();
  }
  void set(jsi::Runtime&, const jsi::PropNameID&, const jsi::Value&) override {}
  std::vector<jsi::PropNameID> getPropertyNames(jsi::Runtime&) override {
    return {};
  }
};

// Stored native info for wrapped objects and functions
struct NativeCallbackData {
  napi_env env;
  napi_callback cb;
  void* data;
};

// Wrap info stored as host object on a hidden property
class WrapInfo : public jsi::HostObject {
 public:
  napi_env env;
  void* native_data{nullptr};
  napi_finalize release_cb{nullptr};
  void* release_hint{nullptr};
  bool is_external{false};

  WrapInfo(napi_env e) : env(e) {}
  ~WrapInfo() override {
    if (release_cb && native_data) release_cb(env, native_data, release_hint);
  }
  jsi::Value get(jsi::Runtime&, const jsi::PropNameID&) override {
    return jsi::Value::undefined();
  }
  void set(jsi::Runtime&, const jsi::PropNameID&, const jsi::Value&) override {}
  std::vector<jsi::PropNameID> getPropertyNames(jsi::Runtime&) override {
    return {};
  }
};

const char* WRAP_KEY = "__BN_wrap__";
const char* EXTERNAL_KEY = "__BN_external__";

WrapInfo* getWrapInfo(napi_env env, const jsi::Object& obj) {
  try {
    auto prop = obj.getProperty(env->runtime, WRAP_KEY);
    if (prop.isObject()) {
      auto ho = prop.getObject(env->runtime).getHostObject<WrapInfo>(env->runtime);
      return ho.get();
    }
  } catch (...) {}
  return nullptr;
}

ExternalValue* getExternalInfo(napi_env env, const jsi::Object& obj) {
  try {
    auto prop = obj.getProperty(env->runtime, EXTERNAL_KEY);
    if (prop.isObject()) {
      auto ho = prop.getObject(env->runtime).getHostObject<ExternalValue>(env->runtime);
      return ho.get();
    }
  } catch (...) {}
  return nullptr;
}

}  // namespace

struct napi_ref__ {
  napi_ref__() = default;
  napi_ref__(const napi_ref__&) = delete;
  napi_ref__& operator=(const napi_ref__&) = delete;

  napi_env env_{nullptr};
  std::optional<jsi::Value> persistent_value_;
  uint32_t refcount_{0};

  napi_status init(napi_env env, napi_value value, uint32_t count) {
    env_ = env;
    persistent_value_ = jsi::Value(env->runtime, value->value);
    refcount_ = count;
    return napi_ok;
  }

  void deinit() {
    persistent_value_.reset();
    refcount_ = 0;
  }

  void ref() { ++refcount_; }

  void unref() {
    assert(refcount_ > 0);
    --refcount_;
  }

  uint32_t count() const { return refcount_; }

  napi_status value(napi_env env, napi_value* result) const {
    if (persistent_value_.has_value()) {
      *result = env->store_value(jsi::Value(env->runtime, *persistent_value_));
    } else {
      *result = nullptr;
    }
    return napi_ok;
  }
};

// Warning: Keep in-sync with napi_status enum
static const char* error_messages[] = {
    nullptr,
    "Invalid argument",
    "An object was expected",
    "A string was expected",
    "A string or symbol was expected",
    "A function was expected",
    "A number was expected",
    "A boolean was expected",
    "An array was expected",
    "Unknown failure",
    "An exception is pending",
    "The async work item was cancelled",
    "napi_escape_handle already called on scope",
    "Invalid handle scope usage",
    "Invalid callback scope usage",
    "Thread-safe function queue is full",
    "Thread-safe function handle is closing",
    "A bigint was expected",
};

napi_status napi_get_last_error_info(napi_env env,
                                     const napi_extended_error_info** result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  static_assert(std::size(error_messages) == napi_bigint_expected + 1,
                "Count of error messages must match count of error values");
  env->last_error.error_message =
      error_messages[env->last_error.error_code];
  *result = &env->last_error;
  return napi_ok;
}

napi_status napi_get_undefined(napi_env env, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value::undefined());
  return napi_ok;
}

napi_status napi_get_null(napi_env env, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value::null());
  return napi_ok;
}

napi_status napi_get_global(napi_env env, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value(env->runtime, env->runtime.global()));
  return napi_ok;
}

napi_status napi_get_boolean(napi_env env, bool value, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value(value));
  return napi_ok;
}

napi_status napi_create_object(napi_env env, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    *result = env->store_value(jsi::Value(env->runtime, jsi::Object(env->runtime)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_array(napi_env env, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto array = env->runtime.global()
                     .getPropertyAsFunction(env->runtime, "Array")
                     .call(env->runtime);
    *result = env->store_value(std::move(array));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_array_with_length(napi_env env, size_t length,
                                          napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "Array");
    auto array = ctor.callAsConstructor(env->runtime, jsi::Value(static_cast<double>(length)));
    *result = env->store_value(std::move(array));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_double(napi_env env, double value,
                               napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value(value));
  return napi_ok;
}

napi_status napi_create_int32(napi_env env, int32_t value,
                              napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value(static_cast<int>(value)));
  return napi_ok;
}

napi_status napi_create_uint32(napi_env env, uint32_t value,
                               napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value(static_cast<double>(value)));
  return napi_ok;
}

napi_status napi_create_int64(napi_env env, int64_t value,
                              napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->store_value(jsi::Value(static_cast<double>(value)));
  return napi_ok;
}

napi_status napi_create_string_latin1(napi_env env, const char* str,
                                      size_t length, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    size_t len = (length == NAPI_AUTO_LENGTH) ? strlen(str) : length;
    auto s = jsi::String::createFromUtf8(env->runtime,
                                         reinterpret_cast<const uint8_t*>(str), len);
    *result = env->store_value(jsi::Value(env->runtime, std::move(s)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_string_utf8(napi_env env, const char* str,
                                    size_t length, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    size_t len = (length == NAPI_AUTO_LENGTH) ? strlen(str) : length;
    auto s = jsi::String::createFromUtf8(env->runtime,
                                         reinterpret_cast<const uint8_t*>(str), len);
    *result = env->store_value(jsi::Value(env->runtime, std::move(s)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_string_utf16(napi_env env, const char16_t* str,
                                     size_t length, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    size_t len = length;
    if (len == NAPI_AUTO_LENGTH) {
      len = 0;
      while (str[len] != 0) ++len;
    }
    // Convert UTF-16 to UTF-8
    std::string utf8;
    utf8.reserve(len * 3);
    for (size_t i = 0; i < len; ++i) {
      char16_t ch = str[i];
      uint32_t cp = ch;
      if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < len) {
        char16_t lo = str[i + 1];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          cp = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00);
          ++i;
        }
      }
      if (cp < 0x80) {
        utf8.push_back(static_cast<char>(cp));
      } else if (cp < 0x800) {
        utf8.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        utf8.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        utf8.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        utf8.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }
    auto s = jsi::String::createFromUtf8(env->runtime, utf8);
    *result = env->store_value(jsi::Value(env->runtime, std::move(s)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_symbol(napi_env env, napi_value description,
                               napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto symbolFunc = env->runtime.global().getPropertyAsFunction(env->runtime, "Symbol");
    jsi::Value sym;
    if (description != nullptr) {
      sym = symbolFunc.call(env->runtime, V(description));
    } else {
      sym = symbolFunc.call(env->runtime);
    }
    *result = env->store_value(std::move(sym));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status node_api_symbol_for(napi_env env, const char* utf8description,
                                size_t length, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto symbolFunc = env->runtime.global().getPropertyAsFunction(env->runtime, "Symbol");
    auto forFunc = symbolFunc.getProperty(env->runtime, "for").asObject(env->runtime).asFunction(env->runtime);
    size_t len = (length == NAPI_AUTO_LENGTH) ? strlen(utf8description) : length;
    auto desc = jsi::String::createFromUtf8(env->runtime, reinterpret_cast<const uint8_t*>(utf8description), len);
    auto sym = forFunc.call(env->runtime, jsi::Value(env->runtime, std::move(desc)));
    *result = env->store_value(std::move(sym));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_function(napi_env env, const char* utf8name,
                                 size_t length, napi_callback cb,
                                 void* callback_data, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    std::string name;
    if (utf8name != nullptr) {
      name = (length == NAPI_AUTO_LENGTH) ? utf8name : std::string(utf8name, length);
    }
    auto hostFn = jsi::Function::createFromHostFunction(
        env->runtime,
        jsi::PropNameID::forUtf8(env->runtime, name),
        0,
        [env, cb, callback_data](jsi::Runtime& rt, const jsi::Value& thisVal,
                                  const jsi::Value* args, size_t count) -> jsi::Value {
          napi_clear_last_error(env);
          std::vector<napi_value> napi_args(count);
          for (size_t i = 0; i < count; ++i) {
            napi_args[i] = env->store_value(jsi::Value(rt, args[i]));
          }
          napi_value thisArg = env->store_value(jsi::Value(rt, thisVal));
          napi_callback_info__ cbinfo{};
          cbinfo.thisArg = thisArg;
          cbinfo.newTarget = nullptr;
          cbinfo.argc = static_cast<uint16_t>(count);
          cbinfo.argv = napi_args.empty() ? nullptr : napi_args.data();
          cbinfo.data = callback_data;

          napi_value retval = cb(env, &cbinfo);

          if (env->last_exception.has_value()) {
            auto exc = std::move(*env->last_exception);
            env->last_exception.reset();
            throw jsi::JSError(rt, std::move(exc));
          }

          if (retval != nullptr) {
            return jsi::Value(rt, retval->value);
          }
          return jsi::Value::undefined();
        });
    *result = env->store_value(jsi::Value(env->runtime, std::move(hostFn)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_define_class(napi_env env, const char* utf8name,
                              size_t length, napi_callback cb, void* data,
                              size_t property_count,
                              const napi_property_descriptor* properties,
                              napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    std::string name;
    if (utf8name != nullptr) {
      name = (length == NAPI_AUTO_LENGTH) ? utf8name : std::string(utf8name, length);
    }

    // Create a native callback wrapper as a host function
    auto nativeInit = jsi::Function::createFromHostFunction(
        env->runtime,
        jsi::PropNameID::forUtf8(env->runtime, "__native_init__"),
        0,
        [env, cb, data](jsi::Runtime& rt, const jsi::Value& thisVal,
                        const jsi::Value* args, size_t count) -> jsi::Value {
          napi_clear_last_error(env);
          // Last arg is the constructor (new.target), rest are real args
          size_t realCount = count > 0 ? count - 1 : 0;
          std::vector<napi_value> napi_args;
          napi_args.reserve(realCount);
          for (size_t i = 0; i < realCount; ++i) {
            napi_args.push_back(env->store_value(jsi::Value(rt, args[i])));
          }
          napi_value thisArg = env->store_value(jsi::Value(rt, thisVal));
          napi_value newTarget = count > 0
            ? env->store_value(jsi::Value(rt, args[count - 1]))
            : nullptr;

          napi_callback_info__ cbinfo{};
          cbinfo.thisArg = thisArg;
          cbinfo.newTarget = newTarget;
          cbinfo.argc = static_cast<uint16_t>(realCount);
          cbinfo.argv = napi_args.empty() ? nullptr : napi_args.data();
          cbinfo.data = data;

          napi_value retval = cb(env, &cbinfo);

          if (env->last_exception.has_value()) {
            auto exc = std::move(*env->last_exception);
            env->last_exception.reset();
            throw jsi::JSError(rt, std::move(exc));
          }

          if (retval != nullptr && retval->value.isObject()) {
            return jsi::Value(rt, retval->value);
          }
          return jsi::Value::undefined();
        });

    // Store nativeInit on global with unique key, then create a real JS constructor via eval.
    static int ctorCounter = 0;
    std::string initKey = "__bn_init_" + std::to_string(ctorCounter++) + "__";
    env->runtime.global().setProperty(env->runtime, initKey.c_str(), std::move(nativeInit));

    std::string ctorCode =
        "(function() {"
        "  var init = globalThis['" + initKey + "'];"
        "  delete globalThis['" + initKey + "'];"
        "  function Ctor() {"
        "    var args = [];"
        "    for (var i = 0; i < arguments.length; i++) args.push(arguments[i]);"
        "    args.push(Ctor);"
        "    var ret = init.apply(this, args);"
        "    if (ret !== undefined && typeof ret === 'object' && ret !== null) return ret;"
        "  }"
        "  return Ctor;"
        "})()";

    auto ctorVal = env->runtime.evaluateJavaScript(
        std::make_shared<jsi::StringBuffer>(ctorCode), "<napi_define_class>");
    napi_value constructor = env->store_value(std::move(ctorVal));

    int instancePropertyCount = 0;
    int staticPropertyCount = 0;
    for (size_t i = 0; i < property_count; i++) {
      if ((properties[i].attributes & napi_static) != 0) {
        staticPropertyCount++;
      } else {
        instancePropertyCount++;
      }
    }

    std::vector<napi_property_descriptor> staticDescriptors;
    std::vector<napi_property_descriptor> instanceDescriptors;
    staticDescriptors.reserve(staticPropertyCount);
    instanceDescriptors.reserve(instancePropertyCount);

    for (size_t i = 0; i < property_count; i++) {
      if ((properties[i].attributes & napi_static) != 0) {
        staticDescriptors.push_back(properties[i]);
      } else {
        instanceDescriptors.push_back(properties[i]);
      }
    }

    if (staticPropertyCount > 0) {
      CHECK_NAPI(napi_define_properties(env, constructor,
                                        staticDescriptors.size(),
                                        staticDescriptors.data()));
    }

    if (instancePropertyCount > 0) {
      napi_value protoVal{};
      CHECK_NAPI(napi_get_named_property(env, constructor, "prototype", &protoVal));
      CHECK_NAPI(napi_define_properties(env, protoVal,
                                        instanceDescriptors.size(),
                                        instanceDescriptors.data()));
    }

    *result = constructor;
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_error(napi_env env, napi_value code, napi_value msg,
                              napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);
  try {
    auto errorCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Error");
    auto error = errorCtor.callAsConstructor(env->runtime, V(msg));
    *result = env->store_value(std::move(error));
    CHECK_NAPI(napi_set_error_code(env, *result, code, nullptr));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_type_error(napi_env env, napi_value code,
                                   napi_value msg, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "TypeError");
    auto error = ctor.callAsConstructor(env->runtime, V(msg));
    *result = env->store_value(std::move(error));
    CHECK_NAPI(napi_set_error_code(env, *result, code, nullptr));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_range_error(napi_env env, napi_value code,
                                    napi_value msg, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "RangeError");
    auto error = ctor.callAsConstructor(env->runtime, V(msg));
    *result = env->store_value(std::move(error));
    CHECK_NAPI(napi_set_error_code(env, *result, code, nullptr));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status node_api_create_syntax_error(napi_env env, napi_value code,
                                         napi_value msg, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "SyntaxError");
    auto error = ctor.callAsConstructor(env->runtime, V(msg));
    *result = env->store_value(std::move(error));
    CHECK_NAPI(napi_set_error_code(env, *result, code, nullptr));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_typeof(napi_env env, napi_value value,
                        napi_valuetype* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  const auto& val = value->value;
  if (val.isUndefined()) {
    *result = napi_undefined;
  } else if (val.isNull()) {
    *result = napi_null;
  } else if (val.isBool()) {
    *result = napi_boolean;
  } else if (val.isNumber()) {
    *result = napi_number;
  } else if (val.isString()) {
    *result = napi_string;
  } else if (val.isSymbol()) {
    *result = napi_symbol;
  } else if (val.isBigInt()) {
    *result = napi_bigint;
  } else if (val.isObject()) {
    auto obj = val.getObject(env->runtime);
    if (obj.isFunction(env->runtime)) {
      *result = napi_function;
    } else {
      // Check if this is an external
      auto ext = getExternalInfo(env, obj);
      if (ext != nullptr) {
        *result = napi_external;
      } else {
        *result = napi_object;
      }
    }
  } else {
    *result = napi_undefined;
  }
  return napi_ok;
}

napi_status napi_get_value_double(napi_env env, napi_value value,
                                  double* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, value->value.isNumber(), napi_number_expected);
  *result = value->value.getNumber();
  return napi_ok;
}

napi_status napi_get_value_int32(napi_env env, napi_value value,
                                 int32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, value->value.isNumber(), napi_number_expected);
  double num = value->value.getNumber();
  *result = std::isfinite(num) ? static_cast<int32_t>(num) : 0;
  return napi_ok;
}

napi_status napi_get_value_uint32(napi_env env, napi_value value,
                                  uint32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, value->value.isNumber(), napi_number_expected);
  double num = value->value.getNumber();
  *result = std::isfinite(num) ? static_cast<uint32_t>(num) : 0;
  return napi_ok;
}

napi_status napi_get_value_int64(napi_env env, napi_value value,
                                 int64_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, value->value.isNumber(), napi_number_expected);
  double num = value->value.getNumber();
  *result = std::isfinite(num) ? static_cast<int64_t>(num) : 0;
  return napi_ok;
}

napi_status napi_get_value_bool(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, value->value.isBool(), napi_boolean_expected);
  *result = value->value.getBool();
  return napi_ok;
}

napi_status napi_get_value_string_latin1(napi_env env, napi_value value,
                                         char* buf, size_t bufsize,
                                         size_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  RETURN_STATUS_IF_FALSE(env, value->value.isString(), napi_string_expected);
  try {
    auto str = value->value.getString(env->runtime).utf8(env->runtime);
    if (buf == nullptr) {
      *result = str.length();
    } else {
      size_t copied = std::min(str.length(), bufsize - 1);
      std::memcpy(buf, str.c_str(), copied);
      buf[copied] = '\0';
      if (result != nullptr) *result = copied;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_value_string_utf8(napi_env env, napi_value value,
                                       char* buf, size_t bufsize,
                                       size_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  RETURN_STATUS_IF_FALSE(env, value->value.isString(), napi_string_expected);
  try {
    auto str = value->value.getString(env->runtime).utf8(env->runtime);
    if (buf == nullptr) {
      *result = str.length();
    } else {
      size_t copied = std::min(str.length(), bufsize - 1);
      std::memcpy(buf, str.c_str(), copied);
      buf[copied] = '\0';
      if (result != nullptr) *result = copied;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_value_string_utf16(napi_env env, napi_value value,
                                        char16_t* buf, size_t bufsize,
                                        size_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  RETURN_STATUS_IF_FALSE(env, value->value.isString(), napi_string_expected);
  try {
    auto utf8 = value->value.getString(env->runtime).utf8(env->runtime);
    // Convert UTF-8 to UTF-16
    std::u16string u16;
    u16.reserve(utf8.size());
    const auto* s = reinterpret_cast<const unsigned char*>(utf8.c_str());
    const auto* end = s + utf8.size();
    while (s < end) {
      uint32_t cp;
      int trail;
      unsigned char lead = *s++;
      if (lead < 0x80) { cp = lead; trail = 0; }
      else if ((lead >> 5) == 0x6) { cp = lead & 0x1F; trail = 1; }
      else if ((lead >> 4) == 0xE) { cp = lead & 0x0F; trail = 2; }
      else if ((lead >> 3) == 0x1E) { cp = lead & 0x07; trail = 3; }
      else { u16.push_back(0xFFFD); continue; }
      if (s + trail > end) { u16.push_back(0xFFFD); break; }
      for (int i = 0; i < trail; ++i) cp = (cp << 6) | (s[i] & 0x3F);
      s += trail;
      if (cp <= 0xFFFF) {
        u16.push_back(static_cast<char16_t>(cp));
      } else {
        cp -= 0x10000;
        u16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
        u16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
      }
    }
    if (buf == nullptr) {
      *result = u16.size();
    } else {
      size_t copied = std::min(u16.size(), bufsize - 1);
      std::memcpy(buf, u16.data(), copied * sizeof(char16_t));
      buf[copied] = 0;
      if (result != nullptr) *result = copied;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_coerce_to_bool(napi_env env, napi_value value,
                                napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto boolFn = env->runtime.global().getPropertyAsFunction(env->runtime, "Boolean");
    auto boolVal = boolFn.call(env->runtime, V(value));
    *result = env->store_value(std::move(boolVal));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_coerce_to_number(napi_env env, napi_value value,
                                  napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto numFn = env->runtime.global().getPropertyAsFunction(env->runtime, "Number");
    auto numVal = numFn.call(env->runtime, V(value));
    *result = env->store_value(std::move(numVal));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_coerce_to_object(napi_env env, napi_value value,
                                  napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto objFn = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto objVal = objFn.call(env->runtime, V(value));
    *result = env->store_value(std::move(objVal));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_coerce_to_string(napi_env env, napi_value value,
                                  napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto strFn = env->runtime.global().getPropertyAsFunction(env->runtime, "String");
    auto strVal = strFn.call(env->runtime, V(value));
    *result = env->store_value(std::move(strVal));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_prototype(napi_env env, napi_value object,
                               napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto objectCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto getProto = objectCtor.getProperty(env->runtime, "getPrototypeOf")
                        .asObject(env->runtime).asFunction(env->runtime);
    auto proto = getProto.call(env->runtime, V(object));
    *result = env->store_value(std::move(proto));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_property_names(napi_env env, napi_value object,
                                    napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto objectCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto fn = objectCtor.getProperty(env->runtime, "getOwnPropertyNames")
                  .asObject(env->runtime).asFunction(env->runtime);
    auto names = fn.call(env->runtime, V(object));
    *result = env->store_value(std::move(names));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

#if NAPI_VERSION >= 6
napi_status napi_get_all_property_names(napi_env env, napi_value object,
                                        napi_key_collection_mode /*key_mode*/,
                                        napi_key_filter /*key_filter*/,
                                        napi_key_conversion /*key_conversion*/,
                                        napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  // Simplified: return own property names (symbols + strings)
  try {
    auto objectCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto fn = objectCtor.getProperty(env->runtime, "getOwnPropertyNames")
                  .asObject(env->runtime).asFunction(env->runtime);
    auto names = fn.call(env->runtime, V(object));
    *result = env->store_value(std::move(names));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}
#endif  // NAPI_VERSION >= 6

napi_status napi_set_property(napi_env env, napi_value object, napi_value key,
                              napi_value value) {
  CHECK_ENV(env);
  CHECK_ARG(env, key);
  CHECK_ARG(env, value);
  try {
    auto obj = object->value.getObject(env->runtime);
    if (key->value.isString()) {
      obj.setProperty(env->runtime,
                      jsi::PropNameID::forString(env->runtime, key->value.getString(env->runtime)),
                      value->value);
    } else {
      auto keyStr = key->value.toString(env->runtime);
      obj.setProperty(env->runtime,
                      jsi::PropNameID::forString(env->runtime, keyStr),
                      value->value);
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_has_property(napi_env env, napi_value object, napi_value key,
                              bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  CHECK_ARG(env, key);
  try {
    auto obj = object->value.getObject(env->runtime);
    if (key->value.isString()) {
      *result = obj.hasProperty(env->runtime,
                                jsi::PropNameID::forString(env->runtime, key->value.getString(env->runtime)));
    } else {
      auto keyStr = key->value.toString(env->runtime);
      *result = obj.hasProperty(env->runtime,
                                jsi::PropNameID::forString(env->runtime, keyStr));
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_property(napi_env env, napi_value object, napi_value key,
                              napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, key);
  CHECK_ARG(env, result);
  try {
    auto obj = object->value.getObject(env->runtime);
    jsi::Value val;
    if (key->value.isString()) {
      val = obj.getProperty(env->runtime,
                            jsi::PropNameID::forString(env->runtime, key->value.getString(env->runtime)));
    } else {
      auto keyStr = key->value.toString(env->runtime);
      val = obj.getProperty(env->runtime,
                            jsi::PropNameID::forString(env->runtime, keyStr));
    }
    *result = env->store_value(std::move(val));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_delete_property(napi_env env, napi_value object,
                                 napi_value key, bool* result) {
  CHECK_ENV(env);
  try {
    // Use JS delete operator via eval-like approach
    auto obj = object->value.getObject(env->runtime);
    auto keyStr = key->value.toString(env->runtime).utf8(env->runtime);
    // Use Reflect.deleteProperty
    auto reflect = env->runtime.global().getProperty(env->runtime, "Reflect");
    if (reflect.isObject()) {
      auto delFn = reflect.getObject(env->runtime)
                       .getPropertyAsFunction(env->runtime, "deleteProperty");
      auto res = delFn.call(env->runtime, V(object), V(key));
      if (result != nullptr) *result = res.getBool();
    } else {
      if (result != nullptr) *result = false;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_has_own_property(napi_env env, napi_value object,
                                  napi_value key, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto objectCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto proto = objectCtor.getProperty(env->runtime, "prototype").asObject(env->runtime);
    auto hasOwn = proto.getPropertyAsFunction(env->runtime, "hasOwnProperty");
    auto res = hasOwn.callWithThis(env->runtime, object->value.getObject(env->runtime), V(key));
    *result = res.getBool();
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_set_named_property(napi_env env, napi_value object,
                                    const char* utf8name, napi_value value) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  try {
    object->value.getObject(env->runtime).setProperty(env->runtime, utf8name, value->value);
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_has_named_property(napi_env env, napi_value object,
                                    const char* utf8name, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  CHECK_ARG(env, result);
  try {
    *result = object->value.getObject(env->runtime).hasProperty(env->runtime, utf8name);
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_named_property(napi_env env, napi_value object,
                                    const char* utf8name, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  CHECK_ARG(env, result);
  try {
    auto val = object->value.getObject(env->runtime).getProperty(env->runtime, utf8name);
    *result = env->store_value(std::move(val));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_set_element(napi_env env, napi_value object, uint32_t index,
                             napi_value value) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  try {
    auto obj = object->value.getObject(env->runtime);
    if (obj.isArray(env->runtime)) {
      obj.getArray(env->runtime).setValueAtIndex(env->runtime, index, value->value);
    } else {
      obj.setProperty(env->runtime,
                      jsi::PropNameID::forUtf8(env->runtime, std::to_string(index)),
                      value->value);
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_has_element(napi_env env, napi_value object, uint32_t index,
                             bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto obj = object->value.getObject(env->runtime);
    auto val = obj.getProperty(env->runtime,
                               jsi::PropNameID::forUtf8(env->runtime, std::to_string(index)));
    *result = !val.isUndefined();
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_element(napi_env env, napi_value object, uint32_t index,
                             napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto obj = object->value.getObject(env->runtime);
    jsi::Value val;
    if (obj.isArray(env->runtime)) {
      val = obj.getArray(env->runtime).getValueAtIndex(env->runtime, index);
    } else {
      val = obj.getProperty(env->runtime,
                            jsi::PropNameID::forUtf8(env->runtime, std::to_string(index)));
    }
    *result = env->store_value(std::move(val));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_delete_element(napi_env env, napi_value object,
                                uint32_t index, bool* result) {
  CHECK_ENV(env);
  try {
    auto reflect = env->runtime.global().getProperty(env->runtime, "Reflect");
    if (reflect.isObject()) {
      auto delFn = reflect.getObject(env->runtime)
                       .getPropertyAsFunction(env->runtime, "deleteProperty");
      auto indexVal = jsi::Value(static_cast<double>(index));
      auto res = delFn.call(env->runtime, V(object), indexVal);
      if (result != nullptr) *result = res.getBool();
    } else {
      if (result != nullptr) *result = false;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_define_properties(napi_env env, napi_value object,
                                   size_t property_count,
                                   const napi_property_descriptor* properties) {
  CHECK_ENV(env);
  if (property_count > 0) CHECK_ARG(env, properties);
  for (size_t i = 0; i < property_count; i++) {
    const napi_property_descriptor* p = properties + i;
    napi_value descriptor{};
    CHECK_NAPI(napi_create_object(env, &descriptor));

    napi_value configurable{};
    CHECK_NAPI(napi_get_boolean(env, (p->attributes & napi_configurable), &configurable));
    CHECK_NAPI(napi_set_named_property(env, descriptor, "configurable", configurable));

    napi_value enumerable{};
    CHECK_NAPI(napi_get_boolean(env, (p->attributes & napi_enumerable), &enumerable));
    CHECK_NAPI(napi_set_named_property(env, descriptor, "enumerable", enumerable));

    if (p->getter != nullptr || p->setter != nullptr) {
      if (p->getter != nullptr) {
        napi_value getter{};
        CHECK_NAPI(napi_create_function(env, p->utf8name, NAPI_AUTO_LENGTH,
                                        p->getter, p->data, &getter));
        CHECK_NAPI(napi_set_named_property(env, descriptor, "get", getter));
      }
      if (p->setter != nullptr) {
        napi_value setter{};
        CHECK_NAPI(napi_create_function(env, p->utf8name, NAPI_AUTO_LENGTH,
                                        p->setter, p->data, &setter));
        CHECK_NAPI(napi_set_named_property(env, descriptor, "set", setter));
      }
    } else if (p->method != nullptr) {
      napi_value method{};
      CHECK_NAPI(napi_create_function(env, p->utf8name, NAPI_AUTO_LENGTH,
                                      p->method, p->data, &method));
      CHECK_NAPI(napi_set_named_property(env, descriptor, "value", method));
    } else {
      RETURN_STATUS_IF_FALSE(env, p->value != nullptr, napi_invalid_arg);
      napi_value writable{};
      CHECK_NAPI(napi_get_boolean(env, (p->attributes & napi_writable), &writable));
      CHECK_NAPI(napi_set_named_property(env, descriptor, "writable", writable));
      CHECK_NAPI(napi_set_named_property(env, descriptor, "value", p->value));
    }

    napi_value propertyName{};
    if (p->utf8name == nullptr) {
      propertyName = p->name;
    } else {
      CHECK_NAPI(napi_create_string_utf8(env, p->utf8name, NAPI_AUTO_LENGTH,
                                         &propertyName));
    }

    napi_value global{}, object_ctor{}, function{};
    CHECK_NAPI(napi_get_global(env, &global));
    CHECK_NAPI(napi_get_named_property(env, global, "Object", &object_ctor));
    CHECK_NAPI(napi_get_named_property(env, object_ctor, "defineProperty", &function));
    napi_value args[] = {object, propertyName, descriptor};
    CHECK_NAPI(napi_call_function(env, object_ctor, function, 3, args, nullptr));
  }
  return napi_ok;
}

napi_status napi_is_array(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    *result = value->value.isObject() &&
              value->value.getObject(env->runtime).isArray(env->runtime);
  } catch (...) {
    *result = false;
  }
  return napi_ok;
}

napi_status napi_get_array_length(napi_env env, napi_value value,
                                  uint32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto obj = value->value.getObject(env->runtime);
    auto lengthVal = obj.getProperty(env->runtime, "length");
    *result = static_cast<uint32_t>(lengthVal.getNumber());
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_strict_equals(napi_env env, napi_value lhs, napi_value rhs,
                               bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, lhs);
  CHECK_ARG(env, rhs);
  CHECK_ARG(env, result);
  *result = jsi::Value::strictEquals(env->runtime, lhs->value, rhs->value);
  return napi_ok;
}

napi_status napi_call_function(napi_env env, napi_value recv, napi_value func,
                               size_t argc, const napi_value* argv,
                               napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, recv);
  if (argc > 0) CHECK_ARG(env, argv);
  try {
    auto fn = func->value.getObject(env->runtime).asFunction(env->runtime);
    std::vector<jsi::Value> jsiArgs;
    jsiArgs.reserve(argc);
    for (size_t i = 0; i < argc; ++i) {
      jsiArgs.push_back(jsi::Value(env->runtime, argv[i]->value));
    }
    jsi::Value ret;
    if (recv->value.isUndefined()) {
      ret = fn.call(env->runtime, static_cast<const jsi::Value*>(jsiArgs.data()), jsiArgs.size());
    } else {
      ret = fn.callWithThis(env->runtime, recv->value.getObject(env->runtime),
                            static_cast<const jsi::Value*>(jsiArgs.data()), jsiArgs.size());
    }
    if (result != nullptr) {
      *result = env->store_value(std::move(ret));
    }
    // Drain microtask queue (promise jobs) after each function call
    // env->runtime.drainMicrotasks();  // disabled for debugging
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_new_instance(napi_env env, napi_value constructor, size_t argc,
                              const napi_value* argv, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, constructor);
  if (argc > 0) CHECK_ARG(env, argv);
  CHECK_ARG(env, result);
  try {
    auto fn = constructor->value.getObject(env->runtime).asFunction(env->runtime);
    std::vector<jsi::Value> jsiArgs;
    jsiArgs.reserve(argc);
    for (size_t i = 0; i < argc; ++i) {
      jsiArgs.push_back(jsi::Value(env->runtime, argv[i]->value));
    }
    auto ret = fn.callAsConstructor(env->runtime, static_cast<const jsi::Value*>(jsiArgs.data()), jsiArgs.size());
    *result = env->store_value(std::move(ret));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_instanceof(napi_env env, napi_value object,
                            napi_value constructor, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  CHECK_ARG(env, result);
  try {
    *result = object->value.isObject() &&
              object->value.getObject(env->runtime).instanceOf(
                  env->runtime, constructor->value.getObject(env->runtime).asFunction(env->runtime));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_cb_info(napi_env env, napi_callback_info cbinfo,
                             size_t* argc, napi_value* argv,
                             napi_value* this_arg, void** data) {
  CHECK_ENV(env);
  CHECK_ARG(env, cbinfo);
  if (argv != nullptr) {
    CHECK_ARG(env, argc);
    size_t i = 0;
    size_t min = std::min(*argc, static_cast<size_t>(cbinfo->argc));
    for (; i < min; i++) argv[i] = cbinfo->argv[i];
    if (i < *argc) {
      for (; i < *argc; i++) {
        argv[i] = env->store_value(jsi::Value::undefined());
      }
    }
  }
  if (argc != nullptr) *argc = cbinfo->argc;
  if (this_arg != nullptr) *this_arg = cbinfo->thisArg;
  if (data != nullptr) *data = cbinfo->data;
  return napi_ok;
}

napi_status napi_get_new_target(napi_env env, napi_callback_info cbinfo,
                                napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, cbinfo);
  CHECK_ARG(env, result);
  *result = cbinfo->newTarget;
  return napi_ok;
}

napi_status napi_throw(napi_env env, napi_value error) {
  CHECK_ENV(env);
  napi_set_exception_value(env, jsi::Value(env->runtime, error->value));
  return napi_ok;
}

napi_status napi_throw_error(napi_env env, const char* code, const char* msg) {
  CHECK_ENV(env);
  napi_value msg_value{}, error{};
  CHECK_NAPI(napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &msg_value));
  napi_value code_value{nullptr};
  if (code != nullptr) {
    CHECK_NAPI(napi_create_string_utf8(env, code, NAPI_AUTO_LENGTH, &code_value));
  }
  CHECK_NAPI(napi_create_error(env, code_value, msg_value, &error));
  return napi_throw(env, error);
}

napi_status napi_throw_type_error(napi_env env, const char* code,
                                  const char* msg) {
  CHECK_ENV(env);
  napi_value msg_value{}, error{};
  CHECK_NAPI(napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &msg_value));
  napi_value code_value{nullptr};
  if (code != nullptr) {
    CHECK_NAPI(napi_create_string_utf8(env, code, NAPI_AUTO_LENGTH, &code_value));
  }
  CHECK_NAPI(napi_create_type_error(env, code_value, msg_value, &error));
  return napi_throw(env, error);
}

napi_status napi_throw_range_error(napi_env env, const char* code,
                                   const char* msg) {
  CHECK_ENV(env);
  napi_value msg_value{}, error{};
  CHECK_NAPI(napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &msg_value));
  napi_value code_value{nullptr};
  if (code != nullptr) {
    CHECK_NAPI(napi_create_string_utf8(env, code, NAPI_AUTO_LENGTH, &code_value));
  }
  CHECK_NAPI(napi_create_range_error(env, code_value, msg_value, &error));
  return napi_throw(env, error);
}

napi_status node_api_throw_syntax_error(napi_env env, const char* code,
                                        const char* msg) {
  CHECK_ENV(env);
  napi_value msg_value{}, error{};
  CHECK_NAPI(napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &msg_value));
  napi_value code_value{nullptr};
  if (code != nullptr) {
    CHECK_NAPI(napi_create_string_utf8(env, code, NAPI_AUTO_LENGTH, &code_value));
  }
  CHECK_NAPI(node_api_create_syntax_error(env, code_value, msg_value, &error));
  return napi_throw(env, error);
}

napi_status napi_is_error(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  napi_value global{}, error_ctor{};
  CHECK_NAPI(napi_get_global(env, &global));
  CHECK_NAPI(napi_get_named_property(env, global, "Error", &error_ctor));
  CHECK_NAPI(napi_instanceof(env, value, error_ctor, result));
  return napi_ok;
}

napi_status napi_is_exception_pending(napi_env env, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = env->last_exception.has_value();
  return napi_ok;
}

napi_status napi_get_and_clear_last_exception(napi_env env,
                                              napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  if (!env->last_exception.has_value()) {
    return napi_get_undefined(env, result);
  }
  *result = env->store_value(std::move(*env->last_exception));
  env->last_exception.reset();
  return napi_clear_last_error(env);
}

napi_status napi_create_external(napi_env env, void* data,
                                 napi_finalize finalize_cb,
                                 void* finalize_hint, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto ext = std::make_shared<ExternalValue>(env, data, finalize_cb, finalize_hint);
    auto obj = jsi::Object::createFromHostObject(env->runtime, ext);
    // Mark as external
    auto marker = jsi::Object::createFromHostObject(env->runtime, ext);
    obj.setProperty(env->runtime, EXTERNAL_KEY, std::move(marker));
    *result = env->store_value(jsi::Value(env->runtime, std::move(obj)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_value_external(napi_env env, napi_value value,
                                    void** result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    if (value->value.isObject()) {
      auto ext = getExternalInfo(env, value->value.getObject(env->runtime));
      *result = ext ? ext->data : nullptr;
    } else {
      *result = nullptr;
    }
  } catch (...) {
    *result = nullptr;
  }
  return napi_ok;
}

napi_status napi_create_reference(napi_env env, napi_value value,
                                  uint32_t initial_refcount,
                                  napi_ref* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  napi_ref__* ref = new napi_ref__{};
  ref->init(env, value, initial_refcount);
  *result = ref;
  return napi_ok;
}

napi_status napi_delete_reference(napi_env env, napi_ref ref) {
  CHECK_ENV(env);
  CHECK_ARG(env, ref);
  ref->deinit();
  delete ref;
  return napi_ok;
}

napi_status napi_reference_ref(napi_env env, napi_ref ref, uint32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, ref);
  ref->ref();
  if (result != nullptr) *result = ref->count();
  return napi_ok;
}

napi_status napi_reference_unref(napi_env env, napi_ref ref,
                                 uint32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, ref);
  ref->unref();
  if (result != nullptr) *result = ref->count();
  return napi_ok;
}

napi_status napi_get_reference_value(napi_env env, napi_ref ref,
                                     napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, ref);
  CHECK_ARG(env, result);
  *result = nullptr;
  CHECK_NAPI(ref->value(env, result));
  return napi_ok;
}

// Stub handle scope implementations (like JSC)
napi_status napi_open_handle_scope(napi_env env, napi_handle_scope* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = reinterpret_cast<napi_handle_scope>(1);
  return napi_ok;
}

napi_status napi_close_handle_scope(napi_env env, napi_handle_scope scope) {
  CHECK_ENV(env);
  CHECK_ARG(env, scope);
  return napi_ok;
}

napi_status napi_open_escapable_handle_scope(
    napi_env env, napi_escapable_handle_scope* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = reinterpret_cast<napi_escapable_handle_scope>(1);
  return napi_ok;
}

napi_status napi_close_escapable_handle_scope(
    napi_env env, napi_escapable_handle_scope scope) {
  CHECK_ENV(env);
  CHECK_ARG(env, scope);
  return napi_ok;
}

napi_status napi_escape_handle(napi_env env,
                               napi_escapable_handle_scope scope,
                               napi_value escapee, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, scope);
  CHECK_ARG(env, escapee);
  CHECK_ARG(env, result);
  *result = escapee;
  return napi_ok;
}

napi_status napi_wrap(napi_env env, napi_value js_object, void* native_object,
                      napi_finalize finalize_cb, void* finalize_hint,
                      napi_ref* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, js_object);
  try {
    auto obj = js_object->value.getObject(env->runtime);
    auto info = getWrapInfo(env, obj);
    if (info == nullptr) {
      auto wi = std::make_shared<WrapInfo>(env);
      wi->native_data = native_object;
      wi->release_cb = finalize_cb;
      wi->release_hint = finalize_hint;
      auto hostObj = jsi::Object::createFromHostObject(env->runtime, wi);
      obj.setProperty(env->runtime, WRAP_KEY, std::move(hostObj));
    } else {
      RETURN_STATUS_IF_FALSE(env, info->native_data == nullptr, napi_invalid_arg);
      info->native_data = native_object;
      info->release_cb = finalize_cb;
      info->release_hint = finalize_hint;
    }
    if (result != nullptr) {
      CHECK_NAPI(napi_create_reference(env, js_object, 0, result));
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_unwrap(napi_env env, napi_value js_object, void** result) {
  CHECK_ENV(env);
  CHECK_ARG(env, js_object);
  try {
    auto info = getWrapInfo(env, js_object->value.getObject(env->runtime));
    RETURN_STATUS_IF_FALSE(env, info != nullptr && info->native_data != nullptr,
                           napi_invalid_arg);
    *result = info->native_data;
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_remove_wrap(napi_env env, napi_value js_object,
                             void** result) {
  CHECK_ENV(env);
  CHECK_ARG(env, js_object);
  try {
    auto info = getWrapInfo(env, js_object->value.getObject(env->runtime));
    RETURN_STATUS_IF_FALSE(env, info != nullptr && info->native_data != nullptr,
                           napi_invalid_arg);
    if (result) *result = info->native_data;
    info->native_data = nullptr;
    info->release_cb = nullptr;
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_is_arraybuffer(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    if (value->value.isObject()) {
      *result = value->value.getObject(env->runtime).isArrayBuffer(env->runtime);
    } else {
      *result = false;
    }
  } catch (...) {
    *result = false;
  }
  return napi_ok;
}

napi_status napi_create_arraybuffer(napi_env env, size_t byte_length,
                                    void** data, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "ArrayBuffer");
    auto ab = ctor.callAsConstructor(env->runtime, jsi::Value(static_cast<double>(byte_length)));
    auto abObj = ab.getObject(env->runtime).getArrayBuffer(env->runtime);
    if (data != nullptr) {
      *data = abObj.data(env->runtime);
    }
    *result = env->store_value(jsi::Value(env->runtime, std::move(abObj)));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_external_arraybuffer(napi_env env, void* external_data,
                                             size_t byte_length,
                                             napi_finalize finalize_cb,
                                             void* finalize_hint,
                                             napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  // JSI doesn't directly support external arraybuffers, create normal one and copy
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "ArrayBuffer");
    auto ab = ctor.callAsConstructor(env->runtime, jsi::Value(static_cast<double>(byte_length)));
    auto abObj = ab.getObject(env->runtime).getArrayBuffer(env->runtime);
    if (external_data != nullptr && byte_length > 0) {
      std::memcpy(abObj.data(env->runtime), external_data, byte_length);
    }
    *result = env->store_value(jsi::Value(env->runtime, std::move(abObj)));
    // Call finalize immediately since we copied the data
    if (finalize_cb != nullptr) {
      finalize_cb(env, external_data, finalize_hint);
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_arraybuffer_info(napi_env env, napi_value arraybuffer,
                                      void** data, size_t* byte_length) {
  CHECK_ENV(env);
  CHECK_ARG(env, arraybuffer);
  try {
    auto ab = arraybuffer->value.getObject(env->runtime).getArrayBuffer(env->runtime);
    if (data != nullptr) *data = ab.data(env->runtime);
    if (byte_length != nullptr) *byte_length = ab.size(env->runtime);
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_is_typedarray(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    if (!value->value.isObject()) { *result = false; return napi_ok; }
    auto global = env->runtime.global();
    auto abView = global.getProperty(env->runtime, "ArrayBuffer");
    if (abView.isObject()) {
      auto isView = abView.getObject(env->runtime).getPropertyAsFunction(env->runtime, "isView");
      auto r = isView.call(env->runtime, V(value));
      // isView returns true for both TypedArray and DataView
      if (r.getBool()) {
        // Check it's not a DataView
        auto dvCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "DataView");
        bool isDv = value->value.getObject(env->runtime).instanceOf(env->runtime, dvCtor);
        *result = !isDv;
      } else {
        *result = false;
      }
    } else {
      *result = false;
    }
  } catch (...) {
    *result = false;
  }
  return napi_ok;
}

napi_status napi_create_typedarray(napi_env env, napi_typedarray_type type,
                                   size_t length, napi_value arraybuffer,
                                   size_t byte_offset, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, arraybuffer);
  CHECK_ARG(env, result);
  try {
    const char* ctorNames[] = {
        "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array",
        "Uint16Array", "Int32Array", "Uint32Array", "Float32Array",
        "Float64Array", "BigInt64Array", "BigUint64Array"};
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, ctorNames[type]);
    auto ta = ctor.callAsConstructor(env->runtime, V(arraybuffer),
                                     jsi::Value(static_cast<double>(byte_offset)),
                                     jsi::Value(static_cast<double>(length)));
    *result = env->store_value(std::move(ta));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_typedarray_info(napi_env env, napi_value typedarray,
                                     napi_typedarray_type* type, size_t* length,
                                     void** data, napi_value* arraybuffer,
                                     size_t* byte_offset) {
  CHECK_ENV(env);
  CHECK_ARG(env, typedarray);
  try {
    auto obj = typedarray->value.getObject(env->runtime);
    if (type != nullptr) {
      // Determine type by checking constructor name
      auto ctorProp = obj.getProperty(env->runtime, "constructor");
      auto ctorName = ctorProp.getObject(env->runtime).getProperty(env->runtime, "name");
      auto name = ctorName.getString(env->runtime).utf8(env->runtime);
      if (name == "Int8Array") *type = napi_int8_array;
      else if (name == "Uint8Array") *type = napi_uint8_array;
      else if (name == "Uint8ClampedArray") *type = napi_uint8_clamped_array;
      else if (name == "Int16Array") *type = napi_int16_array;
      else if (name == "Uint16Array") *type = napi_uint16_array;
      else if (name == "Int32Array") *type = napi_int32_array;
      else if (name == "Uint32Array") *type = napi_uint32_array;
      else if (name == "Float32Array") *type = napi_float32_array;
      else if (name == "Float64Array") *type = napi_float64_array;
      else if (name == "BigInt64Array") *type = napi_bigint64_array;
      else if (name == "BigUint64Array") *type = napi_biguint64_array;
    }
    if (length != nullptr) {
      *length = static_cast<size_t>(obj.getProperty(env->runtime, "length").getNumber());
    }
    if (arraybuffer != nullptr) {
      auto bufProp = obj.getProperty(env->runtime, "buffer");
      *arraybuffer = env->store_value(std::move(bufProp));
    }
    if (byte_offset != nullptr) {
      *byte_offset = static_cast<size_t>(obj.getProperty(env->runtime, "byteOffset").getNumber());
    }
    if (data != nullptr) {
      auto bufProp = obj.getProperty(env->runtime, "buffer");
      auto ab = bufProp.getObject(env->runtime).getArrayBuffer(env->runtime);
      size_t offset = static_cast<size_t>(obj.getProperty(env->runtime, "byteOffset").getNumber());
      *data = static_cast<uint8_t*>(ab.data(env->runtime)) + offset;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_dataview(napi_env env, size_t length,
                                 napi_value arraybuffer, size_t byte_offset,
                                 napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, arraybuffer);
  CHECK_ARG(env, result);
  try {
    auto ctor = env->runtime.global().getPropertyAsFunction(env->runtime, "DataView");
    auto dv = ctor.callAsConstructor(env->runtime, V(arraybuffer),
                                     jsi::Value(static_cast<double>(byte_offset)),
                                     jsi::Value(static_cast<double>(length)));
    *result = env->store_value(std::move(dv));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_is_dataview(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    if (!value->value.isObject()) { *result = false; return napi_ok; }
    auto dvCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "DataView");
    *result = value->value.getObject(env->runtime).instanceOf(env->runtime, dvCtor);
  } catch (...) {
    *result = false;
  }
  return napi_ok;
}

napi_status napi_get_dataview_info(napi_env env, napi_value dataview,
                                   size_t* byte_length, void** data,
                                   napi_value* arraybuffer,
                                   size_t* byte_offset) {
  CHECK_ENV(env);
  CHECK_ARG(env, dataview);
  try {
    auto obj = dataview->value.getObject(env->runtime);
    if (byte_length != nullptr) {
      *byte_length = static_cast<size_t>(obj.getProperty(env->runtime, "byteLength").getNumber());
    }
    if (byte_offset != nullptr) {
      *byte_offset = static_cast<size_t>(obj.getProperty(env->runtime, "byteOffset").getNumber());
    }
    if (arraybuffer != nullptr) {
      auto buf = obj.getProperty(env->runtime, "buffer");
      *arraybuffer = env->store_value(std::move(buf));
    }
    if (data != nullptr) {
      auto buf = obj.getProperty(env->runtime, "buffer");
      auto ab = buf.getObject(env->runtime).getArrayBuffer(env->runtime);
      size_t offset = static_cast<size_t>(obj.getProperty(env->runtime, "byteOffset").getNumber());
      *data = static_cast<uint8_t*>(ab.data(env->runtime)) + offset;
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_version(napi_env env, uint32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = NAPI_VERSION;
  return napi_ok;
}

napi_status napi_create_promise(napi_env env, napi_deferred* deferred,
                                napi_value* promise) {
  CHECK_ENV(env);
  CHECK_ARG(env, deferred);
  CHECK_ARG(env, promise);
  try {
    struct DeferredData {
      std::optional<jsi::Function> resolve;
      std::optional<jsi::Function> reject;
      napi_env env;
    };
    auto data = std::make_shared<DeferredData>();
    data->env = env;

    auto executor = jsi::Function::createFromHostFunction(
        env->runtime, jsi::PropNameID::forAscii(env->runtime, "executor"), 2,
        [data](jsi::Runtime& rt, const jsi::Value&, const jsi::Value* args,
               size_t) -> jsi::Value {
          data->resolve = args[0].getObject(rt).asFunction(rt);
          data->reject = args[1].getObject(rt).asFunction(rt);
          return jsi::Value::undefined();
        });

    auto promiseCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Promise");
    auto promiseObj = promiseCtor.callAsConstructor(env->runtime, std::move(executor));
    *promise = env->store_value(std::move(promiseObj));

    // Prevent shared_ptr DeferredData from going out of scope by storing raw pointer
    auto* rawData = new std::shared_ptr<DeferredData>(data);
    *deferred = reinterpret_cast<napi_deferred>(rawData);
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_resolve_deferred(napi_env env, napi_deferred deferred,
                                  napi_value resolution) {
  CHECK_ENV(env);
  CHECK_ARG(env, deferred);
  CHECK_ARG(env, resolution);
  try {
    struct DeferredData {
      std::optional<jsi::Function> resolve;
      std::optional<jsi::Function> reject;
      napi_env env;
    };
    auto* dataPtr = reinterpret_cast<std::shared_ptr<DeferredData>*>(deferred);
    auto& data = *dataPtr;
    if (data->resolve.has_value()) {
      data->resolve->call(env->runtime, resolution->value);
    }
    delete dataPtr;
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_reject_deferred(napi_env env, napi_deferred deferred,
                                 napi_value rejection) {
  CHECK_ENV(env);
  CHECK_ARG(env, deferred);
  CHECK_ARG(env, rejection);
  try {
    struct DeferredData {
      std::optional<jsi::Function> resolve;
      std::optional<jsi::Function> reject;
      napi_env env;
    };
    auto* dataPtr = reinterpret_cast<std::shared_ptr<DeferredData>*>(deferred);
    auto& data = *dataPtr;
    if (data->reject.has_value()) {
      data->reject->call(env->runtime, rejection->value);
    }
    delete dataPtr;
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_is_promise(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    if (!value->value.isObject()) { *result = false; return napi_ok; }
    auto promiseCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Promise");
    *result = value->value.getObject(env->runtime).instanceOf(env->runtime, promiseCtor);
  } catch (...) {
    *result = false;
  }
  return napi_ok;
}

napi_status napi_run_script(napi_env env, napi_value script,
                            napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, script);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, script->value.isString(), napi_string_expected);
  try {
    auto str = script->value.getString(env->runtime).utf8(env->runtime);
    auto buf = std::make_shared<jsi::StringBuffer>(str);
    auto val = env->runtime.evaluateJavaScript(buf, "<napi_run_script>");
    *result = env->store_value(std::move(val));
    // env->runtime.drainMicrotasks();  // disabled for debugging
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

// Babylon-specific overload with source URL
napi_status napi_run_script(napi_env env, napi_value script,
                            const char* source_url, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, script);
  CHECK_ARG(env, result);
  RETURN_STATUS_IF_FALSE(env, script->value.isString(), napi_string_expected);
  try {
    auto str = script->value.getString(env->runtime).utf8(env->runtime);
    auto buf = std::make_shared<jsi::StringBuffer>(str);
    auto val = env->runtime.evaluateJavaScript(buf, source_url ? source_url : "<napi_run_script>");
    *result = env->store_value(std::move(val));
    // env->runtime.drainMicrotasks();  // disabled for debugging
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_adjust_external_memory(napi_env env, int64_t change_in_bytes,
                                        int64_t* adjusted_value) {
  CHECK_ENV(env);
  CHECK_ARG(env, adjusted_value);
  *adjusted_value = 0;
  return napi_ok;
}

napi_status napi_create_date(napi_env env, double time, napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto dateCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Date");
    auto date = dateCtor.callAsConstructor(env->runtime, jsi::Value(time));
    *result = env->store_value(std::move(date));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_is_date(napi_env env, napi_value value, bool* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    if (!value->value.isObject()) { *result = false; return napi_ok; }
    auto dateCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Date");
    *result = value->value.getObject(env->runtime).instanceOf(env->runtime, dateCtor);
  } catch (...) {
    *result = false;
  }
  return napi_ok;
}

napi_status napi_get_date_value(napi_env env, napi_value value,
                                double* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto obj = value->value.getObject(env->runtime);
    auto getTime = obj.getPropertyAsFunction(env->runtime, "getTime");
    *result = getTime.callWithThis(env->runtime, obj).getNumber();
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_add_finalizer(napi_env env, napi_value js_object,
                               void* native_object, napi_finalize finalize_cb,
                               void* finalize_hint, napi_ref* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, js_object);
  CHECK_ARG(env, finalize_cb);
  try {
    auto obj = js_object->value.getObject(env->runtime);
    auto info = getWrapInfo(env, obj);
    if (info == nullptr) {
      auto wi = std::make_shared<WrapInfo>(env);
      wi->native_data = native_object;
      wi->release_cb = finalize_cb;
      wi->release_hint = finalize_hint;
      auto hostObj = jsi::Object::createFromHostObject(env->runtime, wi);
      obj.setProperty(env->runtime, WRAP_KEY, std::move(hostObj));
    }
    if (result != nullptr) {
      CHECK_NAPI(napi_create_reference(env, js_object, 0, result));
    }
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

// BigInt support
napi_status napi_create_bigint_int64(napi_env env, int64_t value,
                                     napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto bigintFn = env->runtime.global().getPropertyAsFunction(env->runtime, "BigInt");
    auto val = bigintFn.call(env->runtime, jsi::Value(static_cast<double>(value)));
    *result = env->store_value(std::move(val));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_bigint_uint64(napi_env env, uint64_t value,
                                      napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  try {
    auto bigintFn = env->runtime.global().getPropertyAsFunction(env->runtime, "BigInt");
    auto val = bigintFn.call(env->runtime, jsi::Value(static_cast<double>(value)));
    *result = env->store_value(std::move(val));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_create_bigint_words(napi_env env, int sign_bit,
                                     size_t word_count,
                                     const uint64_t* words,
                                     napi_value* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  // Simplified: convert to string representation and create via BigInt()
  try {
    // Build a hex string from the words
    if (word_count == 0) {
      auto bigintFn = env->runtime.global().getPropertyAsFunction(env->runtime, "BigInt");
      *result = env->store_value(bigintFn.call(env->runtime, jsi::Value(0)));
      return napi_ok;
    }
    std::string hex = sign_bit ? "-0x" : "0x";
    for (size_t i = word_count; i > 0; --i) {
      char buf[17];
      snprintf(buf, sizeof(buf), i == word_count ? "%llx" : "%016llx",
               static_cast<unsigned long long>(words[i - 1]));
      hex += buf;
    }
    auto bigintFn = env->runtime.global().getPropertyAsFunction(env->runtime, "BigInt");
    auto hexStr = jsi::String::createFromUtf8(env->runtime, hex);
    *result = env->store_value(bigintFn.call(env->runtime, jsi::Value(env->runtime, std::move(hexStr))));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_value_bigint_int64(napi_env env, napi_value value,
                                        int64_t* result, bool* lossless) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    // Use Number() conversion
    auto numFn = env->runtime.global().getPropertyAsFunction(env->runtime, "Number");
    auto num = numFn.call(env->runtime, V(value));
    double d = num.getNumber();
    *result = static_cast<int64_t>(d);
    if (lossless != nullptr) *lossless = (static_cast<double>(*result) == d);
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_value_bigint_uint64(napi_env env, napi_value value,
                                         uint64_t* result, bool* lossless) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  try {
    auto numFn = env->runtime.global().getPropertyAsFunction(env->runtime, "Number");
    auto num = numFn.call(env->runtime, V(value));
    double d = num.getNumber();
    *result = static_cast<uint64_t>(d);
    if (lossless != nullptr) *lossless = (static_cast<double>(*result) == d);
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_get_value_bigint_words(napi_env env, napi_value value,
                                        int* sign_bit, size_t* word_count,
                                        uint64_t* words) {
  CHECK_ENV(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, word_count);
  // Simplified implementation
  try {
    auto strFn = env->runtime.global().getPropertyAsFunction(env->runtime, "String");
    auto str = strFn.call(env->runtime, V(value)).getString(env->runtime).utf8(env->runtime);

    bool negative = false;
    const char* p = str.c_str();
    if (*p == '-') { negative = true; ++p; }

    // Parse as uint64
    uint64_t val = 0;
    while (*p) {
      if (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
      }
      ++p;
    }

    if (words == nullptr) {
      *word_count = 1;
    } else {
      if (*word_count >= 1) {
        words[0] = val;
        *word_count = 1;
      }
    }
    if (sign_bit != nullptr) *sign_bit = negative ? 1 : 0;
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

#if NAPI_VERSION >= 6
napi_status napi_set_instance_data(napi_env env, void* data,
                                   napi_finalize finalize_cb,
                                   void* finalize_hint) {
  CHECK_ENV(env);
  if (env->instance_data_finalize_cb) {
    env->instance_data_finalize_cb(env, env->instance_data,
                                   env->instance_data_finalize_hint);
  }
  env->instance_data = data;
  env->instance_data_finalize_cb = finalize_cb;
  env->instance_data_finalize_hint = finalize_hint;
  return napi_ok;
}

napi_status napi_get_instance_data(napi_env env, void** data) {
  CHECK_ENV(env);
  CHECK_ARG(env, data);
  *data = env->instance_data;
  return napi_ok;
}
#endif  // NAPI_VERSION >= 6

#if NAPI_VERSION >= 8
napi_status napi_type_tag_object(napi_env env, napi_value object,
                                 const napi_type_tag* type_tag) {
  (void)type_tag;
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  // napi_type_tag requires NAPI_VERSION >= 8, stubbed
  return napi_ok;
}

napi_status napi_check_object_type_tag(napi_env env, napi_value object,
                                       const napi_type_tag* type_tag,
                                       bool* result) {
  (void)type_tag;
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  CHECK_ARG(env, result);
  // napi_type_tag requires NAPI_VERSION >= 8, stubbed
  *result = false;
  return napi_ok;
}

napi_status napi_object_freeze(napi_env env, napi_value object) {
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  try {
    auto objectCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto freezeFn = objectCtor.getProperty(env->runtime, "freeze")
                        .asObject(env->runtime).asFunction(env->runtime);
    freezefn.call(env->runtime, V(object));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}

napi_status napi_object_seal(napi_env env, napi_value object) {
  CHECK_ENV(env);
  CHECK_ARG(env, object);
  try {
    auto objectCtor = env->runtime.global().getPropertyAsFunction(env->runtime, "Object");
    auto sealFn = objectCtor.getProperty(env->runtime, "seal")
                      .asObject(env->runtime).asFunction(env->runtime);
    sealFn.call(env->runtime, V(object));
  } catch (const jsi::JSError& e) {
    return napi_set_exception(env, e);
  }
  return napi_ok;
}
#endif  // NAPI_VERSION >= 8
