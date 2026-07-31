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
    std::vector<napi_value> visited{};
    std::string key{};

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

      // A `getPrototypeOf` Proxy trap can return an object that is already on
      // the chain -- nothing in the specification forbids it, so
      // `Object.getPrototypeOf(p) === p` is reachable from script -- which
      // makes this walk cyclic. V8 recurses and so terminates with a
      // `RangeError`; this loop is iterative and would spin forever.
      //
      // Stopping at the repeat is exact rather than a bail-out: every level
      // adds its own property names to `shadowed` before the walk continues,
      // so a level visited a second time can only re-encounter names that are
      // already shadowed. Breaking here therefore yields the same result the
      // non-terminating walk converges on.
      bool alreadyVisited{};
      RETURN_IF_NOT_OK(Contains(env, visited, current, alreadyVisited));
      if (alreadyVisited) {
        break;
      }
      visited.push_back(current);

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

      napi_value next{};
      RETURN_IF_NOT_OK(napi_get_prototype(env, current, &next));

      bool hasNextLevel{};
      RETURN_IF_NOT_OK(IsObjectLike(env, next, hasNextLevel));
      if (hasNextLevel) {
        napi_value ownNames{};
        RETURN_IF_NOT_OK(napi_call_function(env, objectConstructor, getOwnPropertyNames, 1, &current, &ownNames));
        RETURN_IF_NOT_OK(AddAll(env, ownNames, shadowed));
      }

      current = next;
    }

    *result = names;
    return napi_ok;
  }

  #undef RETURN_IF_NOT_OK
}
