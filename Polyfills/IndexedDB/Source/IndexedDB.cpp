#include <Babylon/Polyfills/IndexedDB.h>
#include <Babylon/Polyfills/Scheduling.h>

#include "IndexedDBScripts.h"

namespace Babylon::Polyfills::IndexedDB
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};
        auto global = env.Global();
        const auto indexedDB = global.Get("indexedDB");
        if (!indexedDB.IsUndefined() && !indexedDB.IsNull())
        {
            return;
        }

        // IndexedDB queues database work as tasks rather than microtasks.
        Scheduling::Initialize(env);
        Napi::Eval(
            env,
            Internal::IndexedDBScripts::Polyfill.data(),
            "jsruntimehost://fake-indexeddb.js");
    }
}
