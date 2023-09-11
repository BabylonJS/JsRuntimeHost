#include <Babylon/Polyfills/Console.h>

#include <array>
#include <functional>
#include <sstream>
#include <regex>

#include <fmt/printf.h>

namespace
{
    constexpr const char* JS_INSTANCE_NAME{"console"};

    void Call(Napi::Function func, const Napi::CallbackInfo& info)
    {
        std::array<Napi::Value, 6> staticArgs{};
        const size_t argc = info.Length();

        if (info.Length() < std::size(staticArgs))
        {
            for (size_t i = 0; i < argc; ++i)
            {
                staticArgs[i] = info[i];
            }

            func.Call(argc, staticArgs.data());
        }
        else
        {
            std::vector<Napi::Value> args(argc);
            for (size_t i = 0; i < argc; ++i)
            {
                args[i] = info[i];
            }

            func.Call(argc, args.data());
        }
    }

    auto TransformArgs(const Napi::CallbackInfo& info) {
        const size_t size = info.Length();
    }

    void InvokeCallback(Babylon::Polyfills::Console::CallbackT callback, const Napi::CallbackInfo& info, Babylon::Polyfills::Console::LogLevel logLevel)
    {
        
        std::string formattedString = "";
        if (info.Length() > 0) {
            formattedString = info[0].ToString().Utf8Value();
            // check if this string has substitutions or not
            std::regex toSub("(%[oOs])|(%(\\d*\\.\\d*)?[dif])");
            std::smatch matches;
            
            // for each argument beyond the first (which is the string itself, try to find a substitution string)
            for (size_t i = 1; i < info.Length(); i++) {
                Napi::Value v = info[i];

                // check if there's a corresponding match to this argument
                if (std::regex_search(formattedString, matches, toSub)) {
                    auto match = matches[0].str();
                    std::string converted;
                    // perform proper formatting
                    if (match == "%o" || match == "%O" || match == "%s") {
                        // object: for now just turn into [Object object]
                        converted = v.ToString().Utf8Value();
                    }
                    else if (match.find("f") != std::string::npos) {
                        // if we have the float specified, force convert to a float
                        Napi::Number number = v.ToNumber();
                        // number cases
                        converted = fmt::sprintf(match, number.DoubleValue());
                    }
                    else {
                        // last case: int specifier
                        Napi::Number number = v.ToNumber();
                        // number cases
                        converted = fmt::sprintf(match, number.Int64Value());
                    }
                    // if converted is nan or -nan, rewrite as NaN to match the javascript format
                    if (converted == "nan" || converted == "-nan") {
                        converted = "NaN";
                    }

                    // replace converted on the original match place
                    size_t start_pos = formattedString.find(match);
                    if (start_pos != std::string::npos) {
                        formattedString.replace(start_pos, match.length(), converted);
                    }
                }
                else {
                    // if there's no corresponding match, just append to the string
                    formattedString = formattedString + " " + v.ToString().Utf8Value();
                }
            }
        }
        formattedString = formattedString + "\n";

        callback(formattedString.c_str(), logLevel);
    }

    void AddMethod(Napi::Object& console, const char* functionName, Babylon::Polyfills::Console::LogLevel logLevel, Babylon::Polyfills::Console::CallbackT callback)
    {
        auto existingFunction = std::make_shared<Napi::FunctionReference>(Napi::Persistent(console.Get(functionName).As<Napi::Function>()));
        console.Set(functionName,
            Napi::Function::New(
                console.Env(), [callback, existingFunction = std::move(existingFunction), logLevel](const Napi::CallbackInfo& info) {
                    InvokeCallback(callback, info, logLevel);

                    if (!existingFunction->Value().IsUndefined())
                    {
                        Call(existingFunction->Value(), info);
                    }
                },
                functionName));
    }
}

namespace Babylon::Polyfills::Console
{
    void Initialize(Napi::Env env, CallbackT callback)
    {
        Napi::HandleScope scope{env};

        auto console = env.Global().Get(JS_INSTANCE_NAME).As<Napi::Object>();
        if (console.IsUndefined())
        {
            console = Napi::Object::New(env);
            env.Global().Set(JS_INSTANCE_NAME, console);
        }

        AddMethod(console, "log", LogLevel::Log, callback);
        AddMethod(console, "warn", LogLevel::Warn, callback);
        AddMethod(console, "error", LogLevel::Error, callback);
    }
}
