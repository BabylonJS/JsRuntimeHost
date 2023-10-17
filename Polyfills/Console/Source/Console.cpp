#include <Babylon/Polyfills/Console.h>

#include <array>
#include <functional>
#include <sstream>
#include <stdio.h>
#include <string>
#include <stdexcept>
#include <string_view>

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

    bool EndsWith(std::string_view s, char c)
    {
        if (!s.empty())
        {
            return s.back() == c;
        }
        return false;
    }

    // based on: https://stackoverflow.com/a/26221725
    template<typename T>
    std::string FormatValue(std::string& formatString, T value, Napi::Env env)
    {
        // get the size of what will be written (+1 for null terminator)
        int size_s = snprintf(nullptr, 0, formatString.c_str(), value) + 1;
        // if size is 0 or less, we had a formatting error
        if (size_s <= 0)
        {
            throw Napi::Error::New(env, "Error during console.log formatting.");
        }
        size_t size = static_cast<size_t>(size_s);
        // create a string to hold that size (-1 as we don't want the null terminator)
        std::string buf(size - 1, '0');
        // write to there
        snprintf(buf.data(), size, formatString.c_str(), value);
        return buf;
    }

    void InvokeCallback(Babylon::Polyfills::Console::CallbackT callback, const Napi::CallbackInfo& info, Babylon::Polyfills::Console::LogLevel logLevel)
    {
        std::ostringstream ss{};
        if (info.Length() > 0)
        {
            std::string firstArg = info[0].ToString();
            size_t currArgIndex = 1;

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
                        ss << prefix;
                    }
                    start = j;
                }
                else if (matching && (currChar == 's' || currChar == 'o' || currChar == 'O' || currChar == 'd' || currChar == 'i' || currChar == 'f'))
                {
                    matching = false;
                    std::string_view prefix(firstArg.data() + start, j - start + 1);
                    // This is a substitution argument, so if we have any remaining arguments to substitute, we should.
                    // Try to use the next arg for subsitution
                    if (currArgIndex < info.Length())
                    {
                        Napi::Value currArg = info[currArgIndex];
                        // Check the type of the sub string
                        if (currChar == 'f')
                        {
                            double d = currArg.ToNumber().DoubleValue();
                            if (std::isnan(d))
                            {
                                ss << "NaN";
                            }
                            else
                            {
                                // I had to copy the string view to a string here, because the data pointer of the string view
                                // returns a view of the entire character sequence, which caused an issue on the following
                                // step when formatting.
                                std::string copiedSubString(prefix);
                                std::string formatted = FormatValue(copiedSubString, d, info.Env());
                                ss << formatted;
                            }
                        }
                        else if (currChar == 'd' || currChar == 'i')
                        {
                            // For some reason, converting to int doesn't result in nans, so I check with double.
                            double d = currArg.ToNumber().DoubleValue();
                            if (std::isnan(d))
                            {
                                ss << "NaN";
                            }
                            else
                            {
                                int64_t n = static_cast<int64_t>(d);
                                // Some explanation as above to why copy to a string
                                std::string copiedSubString(prefix);
                                std::string formatted = FormatValue(copiedSubString, n, info.Env());
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
                        ss << prefix;
                    }
                    start = j + 1;
                }
                else if (matching && (currChar == ' '))
                {
                    matching = false;
                    std::string_view prefix(firstArg.data() + start, j - start + 1);
                    ss << prefix;
                    start = j + 1;
                }
            }
            if (start < j)
            {
                std::string_view prefix(firstArg.data() + start, j - start);
                ss << prefix;
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

        callback(ss.str().c_str(), logLevel);
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