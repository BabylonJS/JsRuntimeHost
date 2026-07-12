#pragma once

#include <napi/napi.h>
#include <JavaScriptCore/JavaScript.h>

#if defined(JSR_USE_BUN_JSC)
extern "C" void* JSCBunAcquireContextLock(JSGlobalContextRef context);
extern "C" void JSCBunReleaseContextLock(void* opaqueLock);
#endif

namespace Napi
{
  Napi::Env Attach(JSGlobalContextRef);

  void Detach(Napi::Env);

  Napi::Value Eval(Napi::Env env, const char* source, const char* sourceUrl);

  JSGlobalContextRef GetContext(Napi::Env);

#if defined(JSR_USE_BUN_JSC)
  class ContextLock final
  {
  public:
    explicit ContextLock(Napi::Env env)
      : m_opaqueLock{JSCBunAcquireContextLock(GetContext(env))}
    {
    }

    ~ContextLock()
    {
      JSCBunReleaseContextLock(m_opaqueLock);
    }

    ContextLock(const ContextLock&) = delete;
    ContextLock& operator=(const ContextLock&) = delete;

  private:
    void* m_opaqueLock{};
  };
#endif
}
