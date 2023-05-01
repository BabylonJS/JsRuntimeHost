#include "WebSocket.h"
#include <Babylon/JsRuntime.h>

namespace Babylon::Polyfills::Internal
{
    void WebSocket::Initialize(Napi::Env env)
    {
        static constexpr auto JS_WEB_SOCKET_CONSTRUCTOR_NAME = "WebSocket";

        if (env.Global().Get(JS_WEB_SOCKET_CONSTRUCTOR_NAME).IsUndefined())
        {
            Napi::Function func = DefineClass(
                env,
                JS_WEB_SOCKET_CONSTRUCTOR_NAME,
                {
                    StaticValue("CONNECTING", Napi::Value::From(env, 0)),
                    StaticValue("OPEN", Napi::Value::From(env, 1)),
                    StaticValue("CLOSING", Napi::Value::From(env, 2)),
                    StaticValue("CLOSED", Napi::Value::From(env, 3)),
                    InstanceAccessor("readyState", &WebSocket::GetReadyState, nullptr),
                    InstanceAccessor("url", &WebSocket::GetURL, nullptr),
                    InstanceAccessor("onopen", nullptr, &WebSocket::SetOnOpen),
                    InstanceAccessor("onclose", nullptr, &WebSocket::SetOnClose),
                    InstanceAccessor("onmessage", nullptr, &WebSocket::SetOnMessage),
                    InstanceAccessor("onerror", nullptr, &WebSocket::SetOnError),
                    InstanceMethod("close", &WebSocket::Close),
                    InstanceMethod("send", &WebSocket::Send),
                });

            env.Global().Set(JS_WEB_SOCKET_CONSTRUCTOR_NAME, func);
        }
    }

    WebSocket::WebSocket(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<WebSocket>{info}
        , m_runtimeScheduler{JsRuntime::GetFromJavaScript(info.Env())}
        , m_webSocket(info[0].As<Napi::String>(),
                      [this, env = info.Env()]()
                      {
                          m_readyState = ReadyState::Open;
                          m_runtimeScheduler([this, env]()
                          {
                              try
                              {
                                  if(!m_onopen.IsEmpty())
                                  {
                                      m_onopen.Call({});
                                  }
                              }
                              catch (...)
                              {
                                  Napi::Error::New(env, std::current_exception())
                                      .ThrowAsJavaScriptException();
                              }
                          });
                      },
                      [this, env = info.Env()]()
                      {
                          m_readyState = ReadyState::Closed;
                          m_runtimeScheduler([this, env]()
                          {
                              try
                              {
                                  Napi::Object closeEvent = Napi::Object::New(Env());
                                  if(!m_onclose.IsEmpty())
                                  {
                                      m_onclose.Call({closeEvent});
                                  }
                              }
                              catch (...)
                              {
                                  Napi::Error::New(env, std::current_exception())
                                      .ThrowAsJavaScriptException();
                              }
                          });
                      },
                      [this, env = info.Env()](std::string message)
                      {
                          m_runtimeScheduler([this, env, message=std::move(message)]()
                          {
                              try
                              {
                                  if(!m_onmessage.IsEmpty())
                                  {
                                      Napi::Object messageEvent = Napi::Object::New(Env());
                                      messageEvent.Set("data", message);
                                      m_onmessage.Call({messageEvent});
                                  }
                              }
                              catch (...)
                              {
                                  Napi::Error::New(env, std::current_exception())
                                      .ThrowAsJavaScriptException();
                              }
                          });
                      },
                      [this, env = info.Env()]()
                      {
                          m_runtimeScheduler([this, env]()
                          {
                              try
                              {
                                  Napi::Object errorEvent = Napi::Object::New(Env());
                                  if(!m_onerror.IsEmpty())
                                  {
                                      m_onerror.Call({errorEvent});
                                  }
                              }
                              catch (...)
                              {
                                  Napi::Error::New(env, std::current_exception())
                                      .ThrowAsJavaScriptException();
                              }
                          });
                      })
    {
        m_url = info[0].As<Napi::String>();
        m_webSocket.Open();
    }

    void WebSocket::Close(const Napi::CallbackInfo &info)
    {
        if (m_readyState == ReadyState::Closed || m_readyState == ReadyState::Closing)
        {
            throw Napi::Error::New(info.Env(), "Close has already been called.");
        }
        m_readyState = ReadyState::Closing;
        m_webSocket.Close();
    }

    void WebSocket::Send(const Napi::CallbackInfo &info)
    {
        if (m_readyState != ReadyState::Open)
        {
            throw Napi::Error::New(info.Env(), "Websocket readyState is not open.");
        }
        std::string message = info[0].As<Napi::String>();
        m_webSocket.Send(message);
    }

    Napi::Value WebSocket::GetReadyState(const Napi::CallbackInfo& )
    {
        return Napi::Value::From(Env(), arcana::underlying_cast(m_readyState));
    }

    Napi::Value WebSocket::GetURL(const Napi::CallbackInfo& )
    {
        return Napi::Value::From(Env(), m_url);
    }

    void WebSocket::SetOnOpen(const Napi::CallbackInfo& , const Napi::Value& value)
    {
        m_onopen = Napi::Persistent(value.As<Napi::Function>());
    }
    
    void WebSocket::SetOnClose(const Napi::CallbackInfo& , const Napi::Value& value)
    {
        m_onclose = Napi::Persistent(value.As<Napi::Function>());
    }

    void WebSocket::SetOnMessage(const Napi::CallbackInfo& , const Napi::Value& value)
    {
        m_onmessage = Napi::Persistent(value.As<Napi::Function>());
    }
    
    void WebSocket::SetOnError(const Napi::CallbackInfo& , const Napi::Value& value)
    {
        m_onerror = Napi::Persistent(value.As<Napi::Function>());
    }
}

namespace Babylon::Polyfills::WebSocket
{
    void Initialize(Napi::Env env)
    {
        Internal::WebSocket::Initialize(env);
    }
}
