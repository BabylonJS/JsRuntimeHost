// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef JSR_JSC_BUN_WEBKIT_REVISION
#error JSR_JSC_BUN_WEBKIT_REVISION must identify the linked Bun WebKit build
#endif

#include <JavaScriptCore/APICast.h>
#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/JSLock.h>
#include <JavaScriptCore/JavaScript.h>

extern "C" void JSCAndroidInitialize()
{
    // Match the standalone JavaScriptCore C API initialization path. In particular, do not call
    // WTF::initializeMainThread(): AppRuntime runs JavaScript on a worker thread, while Android may
    // load this library and initialize other WTF thread-local state on the instrumentation thread.
    JSC::initialize();
}

extern "C" void* JSCAndroidAcquireContextLock(JSGlobalContextRef context)
{
    // Bun's WebKit fork removes the per-call JSLockHolder instances from the C API and expects its
    // embedding event loop to hold the VM lock. This holder is used only for lifecycle operations
    // that must also retain the VM, such as environment teardown and the final context release.
    auto* vm = toJS(JSContextGetGroup(context));
    return new JSC::JSLockHolder(*vm);
}

extern "C" void JSCAndroidReleaseContextLock(void* opaqueLock)
{
    delete static_cast<JSC::JSLockHolder*>(opaqueLock);
}

extern "C" bool JSCAndroidLockContext(JSGlobalContextRef context)
{
    auto* vm = toJS(JSContextGetGroup(context));
    if (vm->currentThreadIsHoldingAPILock())
    {
        return false;
    }

    vm->apiLock().lock();
    return true;
}

extern "C" void JSCAndroidUnlockContext(JSGlobalContextRef context)
{
    toJS(JSContextGetGroup(context))->apiLock().unlock();
}

extern "C" const char* JSCAndroidGetWebKitRevision()
{
    return JSR_JSC_BUN_WEBKIT_REVISION;
}
