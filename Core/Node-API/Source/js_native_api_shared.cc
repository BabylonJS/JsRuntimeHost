#include "js_native_api_shared.h"

#include <napi/js_native_api.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace napi_shared {
  namespace {
    #define RETURN_IF_NOT_OK(expression)                    \
      do {                                                  \
        const napi_status status__{(expression)};           \
        if (status__ != napi_ok) {                          \
          return status__;                                  \
        }                                                   \
      } while (0)

    napi_status GetUtf8Value(napi_env env, napi_value value, std::string& result) {
      size_t length{};
      RETURN_IF_NOT_OK(napi_get_value_string_utf8(env, value, nullptr, 0, &length));

      std::vector<char> buffer(length + 1);
      size_t copied{};
      RETURN_IF_NOT_OK(napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &copied));

      result.assign(buffer.data(), copied);
      return napi_ok;
    }

    napi_status IsObjectLike(napi_env env, napi_value value, bool& result) {
      napi_valuetype type{};
      RETURN_IF_NOT_OK(napi_typeof(env, value, &type));
      result = (type == napi_object || type == napi_function || type == napi_external);
      return napi_ok;
    }

    // Appends every element of the string array `names` to `shadowed`.
    napi_status AddAll(napi_env env, napi_value names, std::unordered_set<std::string>& shadowed) {
      uint32_t count{};
      RETURN_IF_NOT_OK(napi_get_array_length(env, names, &count));

      std::string key{};
      for (uint32_t index = 0; index < count; ++index) {
        napi_value name{};
        RETURN_IF_NOT_OK(napi_get_element(env, names, index, &name));
        RETURN_IF_NOT_OK(GetUtf8Value(env, name, key));
        shadowed.insert(std::move(key));
      }

      return napi_ok;
    }
  }

  napi_status GetEnumerablePropertyNames(napi_env env, napi_value object, napi_value* result) {
    // `Object.keys` reports one level's own enumerable string-keyed properties
    // in specification order, which is exactly what `for...in` visits at that
    // level. `Object.getOwnPropertyNames` additionally reports the
    // non-enumerable ones: `for...in` does not visit those, but they still
    // shadow same-named properties further up the prototype chain, so they have
    // to be tracked as well.
    napi_value global{};
    napi_value objectConstructor{};
    napi_value keys{};
    napi_value getOwnPropertyNames{};
    RETURN_IF_NOT_OK(napi_get_global(env, &global));
    RETURN_IF_NOT_OK(napi_get_named_property(env, global, "Object", &objectConstructor));
    RETURN_IF_NOT_OK(napi_get_named_property(env, objectConstructor, "keys", &keys));
    RETURN_IF_NOT_OK(napi_get_named_property(env, objectConstructor, "getOwnPropertyNames", &getOwnPropertyNames));

    napi_value names{};
    RETURN_IF_NOT_OK(napi_create_array(env, &names));
    uint32_t nameCount{};

    std::unordered_set<std::string> shadowed{};
    std::string key{};

    napi_value current{};
    RETURN_IF_NOT_OK(napi_coerce_to_object(env, object, &current));

    while (true) {
      bool isObjectLike{};
      RETURN_IF_NOT_OK(IsObjectLike(env, current, isObjectLike));
      if (!isObjectLike) {
        break;
      }

      napi_value ownEnumerableNames{};
      RETURN_IF_NOT_OK(napi_call_function(env, objectConstructor, keys, 1, &current, &ownEnumerableNames));

      uint32_t ownEnumerableCount{};
      RETURN_IF_NOT_OK(napi_get_array_length(env, ownEnumerableNames, &ownEnumerableCount));
      for (uint32_t index = 0; index < ownEnumerableCount; ++index) {
        napi_value name{};
        RETURN_IF_NOT_OK(napi_get_element(env, ownEnumerableNames, index, &name));
        RETURN_IF_NOT_OK(GetUtf8Value(env, name, key));
        if (shadowed.find(key) == shadowed.end()) {
          RETURN_IF_NOT_OK(napi_set_element(env, names, nameCount++, name));
        }
      }

      napi_value ownNames{};
      RETURN_IF_NOT_OK(napi_call_function(env, objectConstructor, getOwnPropertyNames, 1, &current, &ownNames));
      RETURN_IF_NOT_OK(AddAll(env, ownNames, shadowed));

      RETURN_IF_NOT_OK(napi_get_prototype(env, current, &current));
    }

    *result = names;
    return napi_ok;
  }

  #undef RETURN_IF_NOT_OK
}
