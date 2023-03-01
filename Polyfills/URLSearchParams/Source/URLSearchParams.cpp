#include "URLSearchParams.h"
#include <string>


namespace Babylon::Polyfills::Internal
{
    void URLSearchParams::Initialize(Napi::Env env)
    {
        Napi::HandleScope scope{env};

        Napi::Function func = DefineClass(
            env,
            JS_URL_SEARCH_PARAMS_CONSTRUCTOR_NAME,
            {
                InstanceMethod("get", &URLSearchParams::Get),
                InstanceMethod("set", &URLSearchParams::Set),
                InstanceMethod("has", &URLSearchParams::Has),
            });

        if (env.Global().Get(JS_URL_SEARCH_PARAMS_CONSTRUCTOR_NAME).IsUndefined())
        {
            env.Global().Set(JS_URL_SEARCH_PARAMS_CONSTRUCTOR_NAME, func);
        }

        JsRuntime::NativeObject::GetFromJavaScript(env).Set(JS_URL_SEARCH_PARAMS_CONSTRUCTOR_NAME, func);
    }

    URLSearchParams::URLSearchParams(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<URLSearchParams>{info}
        , m_runtimeScheduler{JsRuntime::GetFromJavaScript(info.Env())}
    {
        std::string queryStr = info[0].As<Napi::String>();
        parseInputQueryStr(queryStr);
    }

    void URLSearchParams::parseKeyVal(const std::string& subStr)
    {
        auto equalSign = subStr.find("=");
        if (equalSign != std::string::npos)
        {
            auto key = subStr.substr(0, equalSign);
            auto value = subStr.substr(equalSign + 1);

            params_map[key] = value;
        }
    }

    void URLSearchParams::parseInputQueryStr(std::string& queryStr)
    {
        // find all the &'s to determine where each param ends
        // got positions for substring
        if (queryStr.length() <= 1)
        {
            return;
        }
        
        // if first character is ?, remove it
        if (queryStr[0] == '?')
        {
            queryStr.erase(0, 1);
        }
        size_t start = 0;
        size_t end = queryStr.find("&");

        // find the first &, in a while loop
        while (end != std::string::npos)
        {
            // get substring
            std::string mySub = queryStr.substr(start, end - start);
            parseKeyVal(mySub);

            // update start, end
            start = end + 1;
            end = queryStr.find("&", start);
        }

        // now need to parse the last key val pair
        parseKeyVal(queryStr.substr(start));
    }

    Napi::Value URLSearchParams::Get(const Napi::CallbackInfo& info)
    {
        std::string key = info[0].As<Napi::String>();
        std::string result;

        auto element = params_map.find(key);

        // element is not found 
        if (element == params_map.end())
        {
            return Env().Null();
        }
        return Napi::Value::From(Env(), element->second);
    }

    std::string URLSearchParams::GetAllParams()
    {
        
        std::string resultString = "";
        if (params_map.empty())
        {
            return resultString;
        }

        resultString += "?";

        for (const auto& myPair : params_map)
        {
            std::string key = myPair.first;
            std::string value = myPair.second;

            resultString += key;
            resultString += "=";
            resultString += value;
            resultString += "&";
        } 

        // check last value of string 
        char ch = resultString.back();
        if (ch == '&')
        { 
            resultString.pop_back();
        };

        return resultString;
    }


    void URLSearchParams::Set(const Napi::CallbackInfo& info)
    {
        if (info.Length() < 2)
            return;

        std::string key = info[0].As<Napi::String>();
        std::string value = info[1].ToString().Utf8Value();

        params_map[key] = value;
    }

    Napi::Value URLSearchParams::Has(const Napi::CallbackInfo& info)
    {
        std::string key = info[0].As<Napi::String>();
        return Napi::Value::From(Env(), params_map.find(key) != params_map.end());
    }
}

namespace Babylon::Polyfills::URLSearchParams
{
    void Initialize(Napi::Env env)
    {
        Internal::URLSearchParams::Initialize(env);
    }
}
