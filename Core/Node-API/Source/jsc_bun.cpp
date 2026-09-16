// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef JSR_BUN_JSC_WEBKIT_REVISION
#error JSR_BUN_JSC_WEBKIT_REVISION must identify the linked Bun WebKit build
#endif

#include <JavaScriptCore/APICast.h>
#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/JSLock.h>
#include <JavaScriptCore/JavaScript.h>

extern "C" void JSCBunInitialize()
{
    // Match the standalone JavaScriptCore C API initialization path. Do not separately call
    // WTF::initializeMainThread(): the host may load the library and initialize other WTF
    // thread-local state on a different thread from the AppRuntime JavaScript worker.
    JSC::initialize();
}

extern "C" void* JSCBunAcquireContextLock(JSGlobalContextRef context)
{
    // Bun's WebKit fork removes the per-call JSLockHolder instances from the C API and expects its
    // embedding event loop to hold the VM lock. This holder is used only for lifecycle operations
    // that must also retain the VM, such as environment teardown and the final context release.
    auto* vm = toJS(JSContextGetGroup(context));
    return new JSC::JSLockHolder(*vm);
}

extern "C" void JSCBunReleaseContextLock(void* opaqueLock)
{
    delete static_cast<JSC::JSLockHolder*>(opaqueLock);
}

extern "C" bool JSCBunLockContext(JSGlobalContextRef context)
{
    auto* vm = toJS(JSContextGetGroup(context));
    if (vm->currentThreadIsHoldingAPILock())
    {
        return false;
    }

    vm->apiLock().lock();
    return true;
}

extern "C" void JSCBunUnlockContext(JSGlobalContextRef context)
{
    toJS(JSContextGetGroup(context))->apiLock().unlock();
}

extern "C" const char* JSCBunGetWebKitRevision()
{
    return JSR_BUN_JSC_WEBKIT_REVISION;
}
