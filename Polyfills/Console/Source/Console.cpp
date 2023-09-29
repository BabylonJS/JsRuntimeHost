#include <Babylon/Polyfills/Console.h>

#include <array>
#include <functional>
#include <sstream>
#include <regex>
#include <stdio.h>
#include <memory>
#include <string>
#include <stdexcept>

namespace
{
    const std::regex toSub("(%[oOs])|(%(\\d*\\.\\d*)?[dif])");
    constexpr const char* JS_INSTANCE_NAME{ "console" };

    // from: https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf/26221725#26221725
    template<typename... Args>
    std::string string_format(const std::string& format, Args... args)
    {
        int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
        if (size_s <= 0)
        {
            throw std::runtime_error("Error during formatting.");
        }
        auto size = static_cast<size_t>(size_s);
        std::unique_ptr<char[]> buf(new char[size]);
        std::snprintf(buf.get(), size, format.c_str(), args...);
        return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
    }

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

    void InvokeCallback(Babylon::Polyfills::Console::CallbackT callback, const Napi::CallbackInfo& info, Babylon::Polyfills::Console::LogLevel logLevel)
    {
        std::stringstream ss{};
        std::string formattedString{};
        if (info.Length() > 0)
        {
            formattedString = info[0].ToString().Utf8Value();
            ss << formattedString;
            // check if this string has substitutions or not
            std::smatch matches;

            bool hasSubsInFirstString = formattedString.find("%") != std::string::npos && std::regex_search(formattedString, matches, toSub);

            // for each argument beyond the first (which is the string itself, try to find a substitution string)
            for (size_t i = 1; i < info.Length(); i++)
            {
                Napi::Value v = info[i];

                // check if there's a corresponding match to this argument
                if (hasSubsInFirstString && formattedString.find("%") != std::string::npos && std::regex_search(formattedString, matches, toSub))
                {
                    const std::string& match = matches[0].str();
                    std::string converted;
                    // perform proper formatting
                    if (match == "%o" || match == "%O" || match == "%s")
                    {
                        // object: for now just turn into [Object object]
                        converted = v.ToString().Utf8Value();
                    }
                    else if (v.IsNumber() && v.ToString().Utf8Value() != "NaN")
                    {
                        // number formatting
                        // if we have the float specified, force convert to a float
                        Napi::Number number = v.ToNumber();

                        // number cases
                        if (match.find("f") != std::string::npos)
                        {
                            converted = string_format(match, number.DoubleValue());
                        }
                        else
                        {
                            converted = string_format(match, number.Int64Value());
                        }
                    }
                    else
                    {
                        converted = "NaN";
                    }

                    // replace converted on the original match place
                    size_t start_pos = matches.position(0);
                    if (start_pos != std::string::npos)
                    {
                        formattedString.replace(start_pos, match.length(), converted);
                    }
                    // reset the stringstream
                    // see: https://stackoverflow.com/questions/20731/how-do-you-clear-a-stringstream-variable
                    ss.clear();
                    ss.str("");
                    ss << formattedString;
                    formattedString = ss.str();
                }
                else
                {
                    // if there's no corresponding match, just append to the string
                    ss << " " << v.ToString().Utf8Value();
                }
            }
        }
        //formattedString = formattedString + "\n";
        ss << std::endl;
        formattedString = ss.str();

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
        Napi::HandleScope scope{ env };

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