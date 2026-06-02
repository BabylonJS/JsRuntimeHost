#include "File.h"
#include "FileReader.h"

#include <Babylon/Polyfills/File.h>

#include <chrono>
#include <string>

namespace Babylon::Polyfills::Internal
{
    namespace
    {
        constexpr auto JS_FILE_CONSTRUCTOR_NAME = "File";
        constexpr auto JS_BLOB_CONSTRUCTOR_NAME = "Blob";
    }

    void File::Initialize(Napi::Env env)
    {
        auto global = env.Global();

        // Refuse to install if the native Blob polyfill is not present:
        // File delegates its byte storage to a Blob, so without it the
        // constructor cannot produce useful instances. Use IsUndefined()
        // rather than IsFunction() because some JavaScriptCore builds
        // (notably libjavascriptcoregtk on Linux) classify constructors
        // created via JSObjectMakeConstructor as typeof 'object', not
        // 'function', so napi_typeof returns napi_object for them.
        auto blob = global.Get(JS_BLOB_CONSTRUCTOR_NAME);
        if (blob.IsUndefined() || blob.IsNull())
        {
            return;
        }

        // No-op if the runtime already provides a global File.
        if (!global.Get(JS_FILE_CONSTRUCTOR_NAME).IsUndefined())
        {
            return;
        }

        Napi::Function func = DefineClass(
            env,
            JS_FILE_CONSTRUCTOR_NAME,
            {
                InstanceAccessor("size", &File::GetSize, nullptr),
                InstanceAccessor("type", &File::GetType, nullptr),
                InstanceAccessor("name", &File::GetName, nullptr),
                InstanceAccessor("lastModified", &File::GetLastModified, nullptr),
                InstanceMethod("arrayBuffer", &File::ArrayBuffer),
                InstanceMethod("text", &File::Text),
                InstanceMethod("bytes", &File::Bytes),
            });

        global.Set(JS_FILE_CONSTRUCTOR_NAME, func);

        // File should behave as a subtype of Blob: any `instanceof Blob`
        // check over a File must succeed. Babylon.js core does this in
        // fileTools, Offline/database, abstractEngine, and thinNativeEngine,
        // so without inheritance those checks silently fail and the
        // serializer/loader paths take the wrong branch.
        //
        // The internal m_blob composition stays as an implementation
        // detail; only the JS-visible prototype chain is wired so
        // `new File(...) instanceof Blob === true`.
        //
        // Two engine quirks force a dual-path approach:
        //
        // - V8 / Chakra: `func.Get("prototype")` is the real prototype
        //   that instances use, so `setPrototypeOf(func.prototype,
        //   blobProto)` is the natural wire-up.
        // - JSC: napi_define_class wraps JSObjectMakeConstructor, whose
        //   `.prototype` JS property points to Object.prototype, not to
        //   the real prototype JSC assigns to instances (the same quirk
        //   the FileReader constants block in FileReader.cpp documents).
        //   The direct call therefore either tries to mutate
        //   Object.prototype's [[Prototype]] (immutable -> TypeError) or
        //   silently does the wrong thing. The portable workaround is to
        //   instantiate a throwaway File and reach the real prototype via
        //   `Object.getPrototypeOf(tempInstance)`.
        //
        // Strategy: try the direct call first, verify by walking the
        // prototype chain of a probe instance, and only fall back to the
        // temp-instance trick if the chain isn't wired up. This keeps
        // the cost minimal on V8/Chakra (one extra `instanceof Blob`
        // walk) and still recovers on JSC.
        auto objectCtor = global.Get("Object").As<Napi::Object>();
        auto getPrototypeOf = objectCtor.Get("getPrototypeOf").As<Napi::Function>();
        auto setPrototypeOf = objectCtor.Get("setPrototypeOf").As<Napi::Function>();
        auto blobProto = blob.As<Napi::Object>().Get("prototype");

        // Step 1: direct wire-up. Best-effort; on JSC this raises and we
        // swallow.
        auto funcProto = func.Get("prototype");
        setPrototypeOf.Call(objectCtor, {funcProto, blobProto});
        if (env.IsExceptionPending())
        {
            env.GetAndClearPendingException();
        }

        // Step 2: probe instance to verify the chain.
        auto emptyParts = Napi::Array::New(env, 0);
        auto initName = Napi::String::New(env, "");
        auto tempInstance = func.New({emptyParts, initName});
        if (env.IsExceptionPending())
        {
            env.GetAndClearPendingException();
            return;
        }

        auto realProto = getPrototypeOf.Call(objectCtor, {tempInstance});
        if (env.IsExceptionPending())
        {
            env.GetAndClearPendingException();
            return;
        }

        // Walk the chain to check if blobProto is reachable.
        auto cursor = realProto;
        while (!cursor.IsNull() && !cursor.IsUndefined())
        {
            if (cursor.StrictEquals(blobProto))
            {
                return; // direct wire-up succeeded
            }
            cursor = getPrototypeOf.Call(objectCtor, {cursor});
            if (env.IsExceptionPending())
            {
                env.GetAndClearPendingException();
                return;
            }
        }

        // Step 3: fallback for engines where func.prototype != realProto
        // (notably JSC). Set the chain on the real prototype we just
        // discovered via getPrototypeOf(tempInstance).
        setPrototypeOf.Call(objectCtor, {realProto, blobProto});
        if (env.IsExceptionPending())
        {
            // Some engines may reject setPrototypeOf even on the real
            // napi-internal prototype. Swallow so Initialize stays
            // best-effort; `instanceof Blob` will be false on those
            // engines but everything else still works.
            env.GetAndClearPendingException();
        }
    }

    File::File(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<File>{info}
    {
        auto env = info.Env();

        // The WHATWG File constructor takes (fileBits, fileName, [options]).
        // Both fileBits and fileName are required (USVString without
        // `optional`), so missing either is a TypeError per WebIDL bindings.
        if (info.Length() < 2)
        {
            throw Napi::TypeError::New(env,
                "Failed to construct 'File': 2 arguments required, but only " +
                std::to_string(info.Length()) + " present.");
        }

        Napi::Value parts = info[0];
        Napi::Value name = info[1];
        Napi::Value options = info.Length() > 2 ? info[2] : env.Undefined();

        // USVString conversion: undefined -> "undefined", null -> "null",
        // numbers/objects -> their .toString() representation. Napi::Value::
        // ToString() routes through napi_coerce_to_string, which matches
        // these semantics on all three engines.
        m_name = name.ToString().Utf8Value();

        // Default lastModified to the current wall clock in milliseconds,
        // matching Date.now() semantics used by the JS File constructor.
        m_lastModified = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        auto blobOptions = Napi::Object::New(env);

        if (options.IsObject())
        {
            auto optsObj = options.As<Napi::Object>();
            if (optsObj.Has("type"))
            {
                blobOptions.Set("type", optsObj.Get("type"));
            }
            if (optsObj.Has("lastModified"))
            {
                auto lm = optsObj.Get("lastModified");
                if (lm.IsNumber())
                {
                    m_lastModified = lm.As<Napi::Number>().DoubleValue();
                }
            }
        }

        Napi::Value partsArray;
        if (parts.IsArray())
        {
            partsArray = parts;
        }
        else
        {
            partsArray = Napi::Array::New(env, 0);
        }

        // Delegate byte-buffer construction to the native Blob polyfill so
        // we benefit from its existing BlobPart handling (ArrayBuffer,
        // typed array, string, Blob).
        auto blobCtor = env.Global().Get(JS_BLOB_CONSTRUCTOR_NAME).As<Napi::Function>();
        auto blobInstance = blobCtor.New({partsArray, blobOptions});
        m_blob = Napi::Persistent(blobInstance);
    }

    Napi::Value File::GetSize(const Napi::CallbackInfo&)
    {
        return m_blob.Value().Get("size");
    }

    Napi::Value File::GetType(const Napi::CallbackInfo&)
    {
        return m_blob.Value().Get("type");
    }

    Napi::Value File::GetName(const Napi::CallbackInfo& info)
    {
        return Napi::String::New(info.Env(), m_name);
    }

    Napi::Value File::GetLastModified(const Napi::CallbackInfo& info)
    {
        return Napi::Number::New(info.Env(), m_lastModified);
    }

    Napi::Value File::ArrayBuffer(const Napi::CallbackInfo&)
    {
        auto blob = m_blob.Value();
        return blob.Get("arrayBuffer").As<Napi::Function>().Call(blob, {});
    }

    Napi::Value File::Text(const Napi::CallbackInfo&)
    {
        auto blob = m_blob.Value();
        return blob.Get("text").As<Napi::Function>().Call(blob, {});
    }

    Napi::Value File::Bytes(const Napi::CallbackInfo&)
    {
        auto blob = m_blob.Value();
        return blob.Get("bytes").As<Napi::Function>().Call(blob, {});
    }
}

namespace Babylon::Polyfills::File
{
    void BABYLON_API Initialize(Napi::Env env)
    {
        Internal::File::Initialize(env);
        Internal::FileReader::Initialize(env);
    }
}
