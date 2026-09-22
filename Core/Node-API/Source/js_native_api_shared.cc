#include "js_native_api_shared.h"

#include <napi/js_native_api.h>

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

    napi_status IsObjectLike(napi_env env, napi_value value, bool& result) {
      napi_valuetype type{};
      RETURN_IF_NOT_OK(napi_typeof(env, value, &type));
      result = (type == napi_object || type == napi_function || type == napi_external);
      return napi_ok;
    }

    // Whether `value` is strictly equal to something already in `seen`.
    napi_status Contains(napi_env env, const std::vector<napi_value>& seen, napi_value value, bool& result) {
      for (const napi_value candidate : seen) {
        bool equal{};
        RETURN_IF_NOT_OK(napi_strict_equals(env, candidate, value, &equal));
        if (equal) {
          result = true;
          return napi_ok;
        }
      }

      result = false;
      return napi_ok;
    }
  }

  napi_status GetEnumerablePropertyNames(napi_env env, napi_value object, napi_value* result) {
    // Take one own-key snapshot per prototype level, then inspect each
    // descriptor to determine enumerability. This matches `for...in` for
    // proxies, whose `ownKeys` trap must not be invoked twice at one level.
    napi_value global{};
    napi_value objectConstructor{};
    napi_value getOwnPropertyNames{};
    napi_value getOwnPropertyDescriptor{};
    napi_value getPrototypeOf{};
    RETURN_IF_NOT_OK(napi_get_global(env, &global));
    RETURN_IF_NOT_OK(napi_get_named_property(env, global, "Object", &objectConstructor));
    RETURN_IF_NOT_OK(napi_get_named_property(env, objectConstructor, "getOwnPropertyNames", &getOwnPropertyNames));
    RETURN_IF_NOT_OK(napi_get_named_property(env, objectConstructor, "getOwnPropertyDescriptor", &getOwnPropertyDescriptor));
    RETURN_IF_NOT_OK(napi_get_named_property(env, objectConstructor, "getPrototypeOf", &getPrototypeOf));

    napi_value names{};
    RETURN_IF_NOT_OK(napi_create_array(env, &names));
    uint32_t nameCount{};

    std::vector<napi_value> shadowed{};
    std::vector<napi_value> visited{};

    // `ToObject` is what the specification (and the V8 implementation) applies
    // to the argument, so a primitive is wrapped and its properties reported.
    // `null` and `undefined` have no wrapper, and V8 reports that as
    // `napi_object_expected`; check explicitly rather than relying on
    // `napi_coerce_to_object`, whose behaviour for those two values differs
    // between engines (QuickJS yields an empty object, JavaScriptCore throws).
    napi_valuetype type{};
    RETURN_IF_NOT_OK(napi_typeof(env, object, &type));
    if (type == napi_null || type == napi_undefined) {
      return napi_object_expected;
    }

    napi_value current{};
    RETURN_IF_NOT_OK(napi_coerce_to_object(env, object, &current));

    while (true) {
      bool isObjectLike{};
      RETURN_IF_NOT_OK(IsObjectLike(env, current, isObjectLike));
      if (!isObjectLike) {
        break;
      }

      bool alreadyVisited{};
      RETURN_IF_NOT_OK(Contains(env, visited, current, alreadyVisited));
      if (alreadyVisited) {
        RETURN_IF_NOT_OK(napi_throw_range_error(env, nullptr, "Cyclic prototype chain"));
        return napi_pending_exception;
      }
      visited.push_back(current);

      napi_value ownNames{};
      RETURN_IF_NOT_OK(napi_call_function(env, objectConstructor, getOwnPropertyNames, 1, &current, &ownNames));

      uint32_t ownNameCount{};
      RETURN_IF_NOT_OK(napi_get_array_length(env, ownNames, &ownNameCount));
      for (uint32_t index = 0; index < ownNameCount; ++index) {
        napi_value name{};
        RETURN_IF_NOT_OK(napi_get_element(env, ownNames, index, &name));

        bool alreadyShadowed{};
        RETURN_IF_NOT_OK(Contains(env, shadowed, name, alreadyShadowed));
        if (alreadyShadowed) {
          continue;
        }

        napi_value descriptorArgs[]{current, name};
        napi_value descriptor{};
        RETURN_IF_NOT_OK(napi_call_function(
            env, objectConstructor, getOwnPropertyDescriptor, 2, descriptorArgs, &descriptor));

        napi_valuetype descriptorType{};
        RETURN_IF_NOT_OK(napi_typeof(env, descriptor, &descriptorType));
        if (descriptorType == napi_undefined) {
          continue;
        }

        shadowed.push_back(name);

        napi_value enumerableValue{};
        RETURN_IF_NOT_OK(napi_get_named_property(env, descriptor, "enumerable", &enumerableValue));
        bool enumerable{};
        RETURN_IF_NOT_OK(napi_get_value_bool(env, enumerableValue, &enumerable));
        if (enumerable) {
          RETURN_IF_NOT_OK(napi_set_element(env, names, nameCount++, name));
        }
      }

      napi_value next{};
      RETURN_IF_NOT_OK(napi_call_function(env, objectConstructor, getPrototypeOf, 1, &current, &next));

      current = next;
    }

    *result = names;
    return napi_ok;
  }

  #undef RETURN_IF_NOT_OK
}
