#ifndef NODE_API_TYPES_H_
#define NODE_API_TYPES_H_

#include <napi/js_native_api_types.h>

typedef struct napi_callback_scope__* napi_callback_scope;
typedef struct napi_async_context__* napi_async_context;
typedef struct napi_async_work__* napi_async_work;

// Current node-api-cts uses the no-JS finalizer environment spelling in its
// v7 typed-array test. It is ABI-compatible with napi_env for this API level;
// the stricter nogc type distinction arrived later in the public headers.
typedef napi_env node_api_basic_env;

typedef void(NAPI_CDECL* napi_async_execute_callback)(napi_env env,
                                                      void* data);
typedef void(NAPI_CDECL* napi_async_complete_callback)(napi_env env,
                                                       napi_status status,
                                                       void* data);

typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    const char* release;
} napi_node_version;

#endif  // NODE_API_TYPES_H_
