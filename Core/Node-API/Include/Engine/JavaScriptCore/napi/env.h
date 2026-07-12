#pragma once

#include <napi/napi.h>
#include <JavaScriptCore/JavaScript.h>

#if __ANDROID__
extern "C" void* JSCAndroidAcquireContextLock(JSGlobalContextRef context);
extern "C" void JSCAndroidReleaseContextLock(void* opaqueLock);
#endif

namespace Napi
{
  Napi::Env Attach(JSGlobalContextRef);

  void Detach(Napi::Env);

  Napi::Value Eval(Napi::Env env, const char* source, const char* sourceUrl);

  JSGlobalContextRef GetContext(Napi::Env);

#if __ANDROID__
  class ContextLock final
  {
  public:
    explicit ContextLock(Napi::Env env)
      : m_opaqueLock{JSCAndroidAcquireContextLock(GetContext(env))}
    {
    }

    ~ContextLock()
    {
      JSCAndroidReleaseContextLock(m_opaqueLock);
    }

    ContextLock(const ContextLock&) = delete;
    ContextLock& operator=(const ContextLock&) = delete;

  private:
    void* m_opaqueLock{};
  };
#endif
}
