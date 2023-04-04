#include "WebSocket.h"

#include <Babylon/JsRuntime.h>


namespace Babylon::Polyfills::Internal
{

void WebSocket::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        static constexpr auto JS_WEB_SOCKET_CONSTRUCTOR_NAME = "WebSocket";

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

        if (env.Global().Get(JS_WEB_SOCKET_CONSTRUCTOR_NAME).IsUndefined())
        {
            env.Global().Set(JS_WEB_SOCKET_CONSTRUCTOR_NAME, func);
        }

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_WEB_SOCKET_CONSTRUCTOR_NAME, func);
    }

    WebSocket::WebSocket(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<WebSocket>{info}
        , m_runtimeScheduler{JsRuntime::GetFromJavaScript(info.Env())},
        m_webSocket{}
    {
                
        auto onOpenLambda = [this]() {
            m_runtimeScheduler([this]() {
                if(!m_onopen.IsEmpty()) {
                    m_onopen.Call({});
                }
            });
        };
        auto onCloseLambda = [this]() {
            m_runtimeScheduler([this]() {
                Napi::Object closeEvent = Napi::Object::New(Env());
                closeEvent.Set("code", 1000);
                closeEvent.Set("reason", "CLOSED_EVENT NATIVE TEST CLOSE");
                closeEvent.Set("wasClean", true);
                if(!m_onclose.IsEmpty()) {
                    m_onclose.Call({closeEvent});
                }
            });
        };
        
        auto onMessageLambda = [this](std::string message) {
            m_runtimeScheduler([this,message=std::move(message)]() {
                Napi::Object messageEvent = Napi::Object::New(Env());
                messageEvent.Set("data", message);
                if(!m_onmessage.IsEmpty()) {
                    m_onmessage.Call({messageEvent});
                }
            });
        };
        //TODO: lmaskati actually pass error information into here
        auto onErrorLambda = [this]() {
            m_runtimeScheduler([this]() {
                Napi::Object errorEvent = Napi::Object::New(Env());
                if(!m_onerror.IsEmpty()) {
                    m_onerror.Call({errorEvent});
                }

            });
        };
        
        m_webSocket.Open(info[0].As<Napi::String>(), onOpenLambda, onCloseLambda, onMessageLambda, onErrorLambda);
    }

    void WebSocket::Close(const Napi::CallbackInfo&)
    {
        m_webSocket.Close();
    }

    void WebSocket::Send(const Napi::CallbackInfo &info) {
        std::string message = info[0].As<Napi::String>();
        m_webSocket.Send(message);
    }

    Napi::Value WebSocket::GetReadyState(const Napi::CallbackInfo& )
    {
        UrlLib::ReadyState ws_readyState = m_webSocket.GetReadyState();
        return Napi::Value::From(Env(), static_cast<int>(ws_readyState));
    }

    Napi::Value WebSocket::GetURL(const Napi::CallbackInfo& )
    {
        return Napi::Value::From(Env(), m_webSocket.GetURL());
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
