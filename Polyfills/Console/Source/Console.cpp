#include <Babylon/Polyfills/Console.h>

#include <array>
#include <functional>
#include <sstream>
#include <regex>
#include <stdio.h>
#include <memory>
#include <string>
#include <stdexcept>
#include <string_view>
#include <cstdarg>
#include <vector>

namespace
{
    // Struct used for splitting string into parts
    struct STRING_PART
    {
        std::string_view value;
        bool isSub;
    };
    constexpr const char* JS_INSTANCE_NAME{ "console" };

    // from: https://stackoverflow.com/a/49812018
    const std::string vformat(const char* const zcFormat, ...)
    {
        // initialize use of the variable argument array
        va_list vaArgs;
        va_start(vaArgs, zcFormat);

        // reliably acquire the size
        // from a copy of the variable argument array
        // and a functionally reliable call to mock the formatting
        va_list vaArgsCopy;
        va_copy(vaArgsCopy, vaArgs);
        const int iLen = std::vsnprintf(NULL, 0, zcFormat, vaArgsCopy);
        va_end(vaArgsCopy);

        // return a formatted string without risking memory mismanagement
        // and without assuming any compiler or platform specific behavior
        std::vector<char> zc(iLen + 1);
        std::vsnprintf(zc.data(), zc.size(), zcFormat, vaArgs);
        va_end(vaArgs);
        return std::string(zc.data(), iLen);
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

    bool EndsWith(std::string_view s, char c) {
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
            std::vector<StringPart> parts{};

            // Split the first string into parts limited by substitution characters
            size_t start = 0;
            bool matching = false;
            std::size_t j;
            for (j = 0; j < firstArg.size(); j++)
            {
                const char currChar = firstArg.at(j);
                if (!matching && currChar == '%')
                {
                    matching = true;
                    if (j > 0)
                    {
                        std::string_view prefix(firstArg.data() + start, j - start);
                        parts.push_back({prefix, false});
                    }
                    start = j;
                }
                else if (matching && (currChar == 's' || currChar == 'o' || currChar == 'O' || currChar == 'd' || currChar == 'i' || currChar == 'f'))
                {
                    matching = false;
                    std::string_view prefix(firstArg.data() + start, j - start + 1);
                    parts.push_back({prefix, true});
                    start = j + 1;
                }
                else if (matching && (currChar == ' '))
                {
                    matching = false;
                    std::string_view prefix(firstArg.data() + start, j - start + 1);
                    parts.push_back({prefix, false});
                    start = j + 1;
                }
            }
            if (start < j)
            {
                std::string_view prefix(firstArg.data() + start, j - start);
                parts.push_back({prefix, false});
            }

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
                        std::string_view subString = part.value;
                        if (EndsWith(subString, 'f'))
                        {
                            double d = currArg.ToNumber().DoubleValue();
                            if (std::isnan(d))
                            {
                                ss << "NaN";
                            }
                            else
                            {
                                // I had to copy the string view to a string here, because the data pointer of the string view
                                // returns a view of the entire character sequence
                                std::string copiedSubString(subString); 
                                std::string formatted = vformat(copiedSubString.c_str(), d);
                                ss << formatted;
                            }
                        }
                        else if (EndsWith(subString, 'd') || EndsWith(subString, 'i'))
                        {
                            // For some reason, converting to int doesn't result in nans, so I check with double.
                            double d = currArg.ToNumber().DoubleValue();
                            if (std::isnan(d))
                            {
                                ss << "NaN";
                            }
                            else
                            {
                                int64_t n = currArg.ToNumber().Int64Value();
                                // Some explanation as above to why copy to a string
                                std::string copiedSubString(subString);
                                std::string formatted = vformat(copiedSubString.c_str(), n);
                                ss << formatted;
                            }
                        }
                        else // 'o', 'O', 's'
                        {
                            ss << currArg.ToString().Utf8Value();
                        }
                        currArgIndex++;
                    }
                    else
                    {
                        ss << part.value;
                    }
                }
                else
                {
                    ss << part.value;
                }
            }
            // if any arguments are remaining after we done all substitutions we could, then dump them into the stream
            for (; currArgIndex < info.Length(); currArgIndex++)
            {   
                ss << " ";
                Napi::Value currArg = info[currArgIndex];
                ss << currArg.ToString().Utf8Value();
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