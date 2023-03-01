#pragma once

#include <Babylon/JsRuntimeScheduler.h>
#include <napi/napi.h>
#include <unordered_map>
#include "../Include/Babylon/Polyfills/URLSearchParams.h"

namespace Babylon::Polyfills::Internal
{
    class URLSearchParams final : public Napi::ObjectWrap<URLSearchParams>
    {
    public:
        static void Initialize(Napi::Env env);
        explicit URLSearchParams(const Napi::CallbackInfo& info);
        static constexpr auto JS_URL_SEARCH_PARAMS_CONSTRUCTOR_NAME = "URLSearchParams";
        std::string GetAllParams();

    private:
        void Set(const Napi::CallbackInfo& info);
        Napi::Value Has(const Napi::CallbackInfo& info);
        Napi::Value Get(const Napi::CallbackInfo& info);
        void parseKeyVal(const std::string& subStr);
        void parseInputQueryStr(std::string& queryStr);

        JsRuntimeScheduler m_runtimeScheduler;
        std::unordered_map<std::string, std::string> params_map;
    };
}
