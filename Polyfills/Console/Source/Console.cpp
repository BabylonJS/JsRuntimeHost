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
    struct STRING_PART
    {
        std::string value;
        bool isSub;
    };
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
    
    /*
    * Given a string, this will split its substitution parameters
    * Example: "foo %s bar" will be split into ["foo", "%s", "bar"]
    * The parameters will be later substituted for arguments
    */
    std::vector<STRING_PART> SplitString(std::string s) {
        std::vector<STRING_PART> result = std::vector<STRING_PART>();

        size_t start = 0;
        bool matching = false;
        std::size_t i;
        for (i = 0; i < s.size(); i++)
        {
            const char currChar = s.at(i);
            if (!matching && currChar == '%')
            {
                matching = true;
                if (i > 0)
                {
                    std::string prefix = s.substr(start, i - start);
                    result.push_back({prefix, false});
                }
                start = i;
            }
            else if (matching && (currChar == 's' || currChar == 'o' || currChar == 'O' || currChar == 'd' || currChar == 'i' || currChar == 'f'))
            {
                matching = false;
                std::string prefix = s.substr(start, i - start + 1);
                result.push_back({prefix, true});
                start = i + 1;
            }
        }
        if (start < i)
        {
            std::string prefix = s.substr(start, i - start);
            result.push_back({prefix, false});
        }

        return result;
    }

    bool EndsWith(std::string s, char c) {
        if (s.size() > 0)
        {
            return s.at(s.size() - 1) == c;
        }
        return false;
    }

    void InvokeCallback(Babylon::Polyfills::Console::CallbackT callback, const Napi::CallbackInfo& info, Babylon::Polyfills::Console::LogLevel logLevel)
    {
        std::stringstream ss{};
        std::string formattedString{};
        if (info.Length() > 0)
        {
            std::string firstArg = info[0].ToString();
            std::vector<STRING_PART> parts = SplitString(firstArg);

            size_t currArgIndex = 1;

            // Go over the split string, substituting when necessary
            for (size_t i = 0; i < parts.size(); i++)
            {
                STRING_PART part = parts.at(i);

                if (part.isSub)
                {
                    // Try to use the next arg for subsitution
                    if (currArgIndex < info.Length())
                    {
                        Napi::Value currArg = info[currArgIndex];
                        // Check the type of the sub string
                        std::string subString = part.value;
                        if (EndsWith(subString, 'f'))
                        {
                            double d = currArg.ToNumber().DoubleValue();
                            if (std::isnan(d))
                            {
                                ss << "NaN";
                            }
                            else
                            {
                                ss << d;
                            }
                        }
                        else if (EndsWith(subString, 'd') || EndsWith(subString, 'i'))
                        {
                            int i = currArg.ToNumber().Int64Value();
                            if (std::isnan(i))
                            {
                                ss << "NaN";
                            }
                            else
                            {
                                ss << i;
                            }
                        }
                        else // 'o', 'O', 's'
                        {
                            ss << currArg.ToString().Utf8Value();
                        }
                        currArgIndex++;
                    }
                }
                else
                {
                    ss << part.value;
                    if (currArgIndex < info.Length() - 1)
                    {
                        ss << " ";
                    }
                }
            }
            // if any arguments are remaining after we done all substitutions we could, then dump them into the stream
            for (; currArgIndex < info.Length(); currArgIndex++)
            {
                Napi::Value currArg = info[currArgIndex];
                ss << currArg.ToString().Utf8Value();
                if (currArgIndex < info.Length() - 1)
                {
                    ss << " ";
                }
            }
        }
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