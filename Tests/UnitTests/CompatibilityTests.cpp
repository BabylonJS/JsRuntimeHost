#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/AbortController.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/Blob.h>
#include <gtest/gtest.h>
#include <future>
#include <chrono>
#include <tuple>
#include <vector>
#include <string>
#include <utility>
#include <atomic>

namespace
{
    class EngineCompatTest : public ::testing::Test
    {
    protected:
        Babylon::AppRuntime& Runtime()
        {
            if (!m_runtime)
            {
                m_runtime = std::make_unique<Babylon::AppRuntime>();
                m_runtime->Dispatch([](Napi::Env env) {
                    Babylon::Polyfills::Console::Initialize(env, [](const char*, Babylon::Polyfills::Console::LogLevel) {});
                    Babylon::Polyfills::AbortController::Initialize(env);
                    Babylon::Polyfills::Scheduling::Initialize(env);
                    Babylon::Polyfills::URL::Initialize(env);
                    Babylon::Polyfills::Blob::Initialize(env);
                });

                m_loader = std::make_unique<Babylon::ScriptLoader>(*m_runtime);
            }

            return *m_runtime;
        }

        Babylon::ScriptLoader& Loader()
        {
            if (!m_loader)
            {
                Runtime();
            }
            return *m_loader;
        }

        void TearDown() override
        {
            m_loader.reset();
            m_runtime.reset();
        }

        template<typename T>
        T Await(std::future<T>& future, std::chrono::milliseconds timeout = std::chrono::milliseconds{5000})
        {
            const auto status = future.wait_for(timeout);
            EXPECT_EQ(status, std::future_status::ready) << "JavaScript did not report back to native code.";
            if (status != std::future_status::ready)
            {
                throw std::runtime_error{"Timeout waiting for JavaScript result"};
            }
            return future.get();
        }

        void Eval(const std::string& script)
        {
            Loader().Eval(script.c_str(), "engine-compat");
        }

    private:
        std::unique_ptr<Babylon::AppRuntime> m_runtime{};
        std::unique_ptr<Babylon::ScriptLoader> m_loader{};
    };
}

TEST_F(EngineCompatTest, LargeStringRoundtrip)
{
    std::promise<size_t> lengthPromise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&lengthPromise](const Napi::CallbackInfo& info) {
            if (info.Length() < 1 || !info[0].IsString())
            {
                ADD_FAILURE() << "nativeCheckLargeString expected a string argument.";
                lengthPromise.set_value(0);
                return;
            }

            const auto value = info[0].As<Napi::String>().Utf8Value();
            if (value.size() != 1'000'000u)
            {
                ADD_FAILURE() << "Large string length mismatch: expected 1,000,000 got " << value.size();
            }
            if (!value.empty())
            {
                if (value.front() != 'x' || value.back() != 'x')
                {
                    ADD_FAILURE() << "Large string boundary characters were not preserved.";
                }
            }

            lengthPromise.set_value(value.size());
        }, "nativeCheckLargeString");

        env.Global().Set("nativeCheckLargeString", fn);
    });

    Eval("const s = 'x'.repeat(1_000_000); nativeCheckLargeString(s);");

    auto future = lengthPromise.get_future();
    EXPECT_EQ(Await(future), 1'000'000u);
}

// approximates a Hermes embedding issue triggered by MobX in another project
TEST_F(EngineCompatTest, SymbolCrossing)
{
    std::promise<std::tuple<bool, bool, std::string>> donePromise;
    std::promise<bool> nativeRoundtripPromise;

    auto completionFlag = std::make_shared<std::atomic<bool>>(false);
    auto roundtripFlag = std::make_shared<std::atomic<bool>>(false);

    Runtime().Dispatch([&](Napi::Env env) {
        auto nativeSymbol = Napi::Symbol::New(env, "native-roundtrip");
        env.Global().Set("nativeSymbolFromCpp", nativeSymbol);

        auto fn = Napi::Function::New(env, [completionFlag, &donePromise](const Napi::CallbackInfo& info) {
            std::tuple<bool, bool, std::string> result{true, false, {}};
            try
            {
                if (info.Length() == 3 && info[0].IsBoolean() && info[1].IsBoolean() && info[2].IsString())
                {
                    result = {
                        info[0].As<Napi::Boolean>().Value(),
                        info[1].As<Napi::Boolean>().Value(),
                        info[2].As<Napi::String>().Utf8Value()
                    };
                }
                else
                {
                    ADD_FAILURE() << "nativeCheckSymbols expected (bool, bool, string) arguments.";
                }
            }
            catch (const std::exception& e)
            {
                ADD_FAILURE() << "nativeCheckSymbols threw exception: " << e.what();
            }
            catch (...)
            {
                ADD_FAILURE() << "nativeCheckSymbols threw an unknown exception.";
            }

            if (!completionFlag->exchange(true))
            {
                try
                {
                    donePromise.set_value(std::move(result));
                }
                catch (const std::exception& e)
                {
                    ADD_FAILURE() << "Failed to fulfill symbol promise: " << e.what();
                }
            }
        }, "nativeCheckSymbols");

        env.Global().Set("nativeCheckSymbols", fn);

        auto validateNativeSymbolFn = Napi::Function::New(env, [roundtripFlag, &nativeRoundtripPromise](const Napi::CallbackInfo& info) {
            bool matches = false;
            try
            {
                if (info.Length() > 0 && info[0].IsSymbol())
                {
                    auto stored = info.Env().Global().Get("nativeSymbolFromCpp");
                    if (stored.IsSymbol())
                    {
                        matches = info[0].As<Napi::Symbol>().StrictEquals(stored.As<Napi::Symbol>());
                    }
                    else
                    {
                        ADD_FAILURE() << "nativeSymbolFromCpp was not a symbol when validated.";
                    }
                }
                else
                {
                    ADD_FAILURE() << "nativeValidateNativeSymbol expected a symbol argument.";
                }
            }
            catch (const std::exception& e)
            {
                ADD_FAILURE() << "nativeValidateNativeSymbol threw exception: " << e.what();
            }
            catch (...)
            {
                ADD_FAILURE() << "nativeValidateNativeSymbol threw an unknown exception.";
            }

            if (!roundtripFlag->exchange(true))
            {
                nativeRoundtripPromise.set_value(matches);
            }
        }, "nativeValidateNativeSymbol");

        env.Global().Set("nativeValidateNativeSymbol", validateNativeSymbolFn);
    });

    Eval(
        "const sym1 = Symbol('test');"
        "const sym2 = Symbol('test');"
        "const sym3 = Symbol.for('global');"
        "const sym4 = Symbol.for('global');"
        "nativeValidateNativeSymbol(nativeSymbolFromCpp);"
        "nativeCheckSymbols(sym1 === sym2, sym3 === sym4, Symbol.keyFor(sym3));");

    auto symbolFuture = donePromise.get_future();
    auto [sym1EqualsSym2, sym3EqualsSym4, sym3String] = Await(symbolFuture);
    EXPECT_FALSE(sym1EqualsSym2);
    EXPECT_TRUE(sym3EqualsSym4);
    EXPECT_NE(sym3String.find("global"), std::string::npos);

    auto nativeFuture = nativeRoundtripPromise.get_future();
    EXPECT_TRUE(Await(nativeFuture)) << "Native-created symbol did not survive JS roundtrip.";
}

TEST_F(EngineCompatTest, Utf16SurrogatePairs)
{
    struct Result
    {
        std::u16string value;
        uint32_t high;
        uint32_t low;
        std::vector<std::string> spread;
    };

    std::promise<Result> resultPromise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&resultPromise](const Napi::CallbackInfo& info) {
            Result result{};
            if (info.Length() == 4 && info[0].IsString() && info[1].IsNumber() && info[2].IsNumber() && info[3].IsArray())
            {
                result.value = info[0].As<Napi::String>().Utf16Value();
                result.high = info[1].As<Napi::Number>().Uint32Value();
                result.low = info[2].As<Napi::Number>().Uint32Value();

                auto array = info[3].As<Napi::Array>();
                for (uint32_t i = 0; i < array.Length(); ++i)
                {
                    result.spread.emplace_back(array.Get(i).As<Napi::String>().Utf8Value());
                }
            }
            else
            {
                ADD_FAILURE() << "nativeCheckUtf16 received unexpected arguments.";
            }

            resultPromise.set_value(std::move(result));
        }, "nativeCheckUtf16");

        env.Global().Set("nativeCheckUtf16", fn);
    });

    Eval(
        "const emoji = '😀🎉🚀';"
        "nativeCheckUtf16(emoji, emoji.charCodeAt(0), emoji.charCodeAt(1), Array.from(emoji));");

    auto future = resultPromise.get_future();
    auto result = Await(future);
    EXPECT_EQ(result.value.size(), 6u);
    EXPECT_EQ(result.high, 0xD83D);
    EXPECT_EQ(result.low, 0xDE00);
    EXPECT_EQ(result.spread.size(), 3u);
    if (result.spread.size() >= 1)
    {
        EXPECT_EQ(result.spread[0], "\xF0\x9F\x98\x80"); // 😀
    }
}

TEST_F(EngineCompatTest, UnicodePlanes)
{
    struct Result
    {
        std::string bmp;
        std::u16string supplementary;
        std::string combining;
        std::string normalizedNfc;
        std::string normalizedNfd;
    };

    std::promise<Result> resultPromise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&resultPromise](const Napi::CallbackInfo& info) {
            Result result{};
            if (info.Length() == 5 && info[0].IsString() && info[1].IsString() && info[2].IsString() && info[3].IsString() && info[4].IsString())
            {
                result.bmp = info[0].As<Napi::String>().Utf8Value();
                result.supplementary = info[1].As<Napi::String>().Utf16Value();
                result.combining = info[2].As<Napi::String>().Utf8Value();
                result.normalizedNfc = info[3].As<Napi::String>().Utf8Value();
                result.normalizedNfd = info[4].As<Napi::String>().Utf8Value();
            }
            else
            {
                ADD_FAILURE() << "nativeCheckUnicode received unexpected arguments.";
            }

            resultPromise.set_value(std::move(result));
        }, "nativeCheckUnicode");

        env.Global().Set("nativeCheckUnicode", fn);
    });

    Eval(
        "const bmp = 'Hello 你好 مرحبا';"
        "const supplementary = '𐍈𐍉𐍊';"
        "const combining = 'é';"
        "const nfc = combining.normalize('NFC');"
        "const nfd = combining.normalize('NFD');"
        "nativeCheckUnicode(bmp, supplementary, combining, nfc, nfd);");

    auto future = resultPromise.get_future();
    auto result = Await(future);
    EXPECT_EQ(result.bmp, "Hello 你好 مرحبا");
    EXPECT_EQ(result.supplementary.size(), 6u);
    EXPECT_EQ(result.combining, "é");
    EXPECT_EQ(result.normalizedNfc, "é");
    EXPECT_GE(result.normalizedNfd.size(), 1u);
}

TEST_F(EngineCompatTest, TextEncoderDecoder)
{
    struct Result
    {
        bool available{};
        std::string expected;
        std::string decoded;
        size_t byteLength{};
    };

    std::promise<Result> resultPromise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&resultPromise](const Napi::CallbackInfo& info) {
            Result result{};
            result.available = info[0].As<Napi::Boolean>().Value();
            if (result.available)
            {
                result.expected = info[1].As<Napi::String>().Utf8Value();
                result.decoded = info[2].As<Napi::String>().Utf8Value();
                result.byteLength = info[3].As<Napi::Number>().Uint32Value();
            }
            resultPromise.set_value(std::move(result));
        }, "nativeTextEncodingResult");

        env.Global().Set("nativeTextEncodingResult", fn);
    });

    Eval(
        "if (typeof TextEncoder === 'undefined' || typeof TextDecoder === 'undefined') {"
        "    nativeTextEncodingResult(false);"
        "} else {"
        "    const encoder = new TextEncoder();"
        "    const decoder = new TextDecoder();"
        "    const text = 'Hello 世界 🌍';"
        "    const encoded = encoder.encode(text);"
        "    const decoded = decoder.decode(encoded);"
        "    nativeTextEncodingResult(true, text, decoded, encoded.length);"
        "}");

    auto future = resultPromise.get_future();
    auto result = Await(future);
    if (!result.available)
    {
        GTEST_SKIP() << "TextEncoder/TextDecoder not available in this engine.";
    }

    EXPECT_EQ(result.decoded, result.expected);
    EXPECT_GT(result.byteLength, 0u);
}

TEST_F(EngineCompatTest, LargeTypedArrayRoundtrip)
{
    std::promise<size_t> promise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            size_t length = 0;
            if (info.Length() == 1 && info[0].IsTypedArray())
            {
                auto array = info[0].As<Napi::Uint8Array>();
                length = array.ElementLength();
                const size_t expectedLength = 10u * 1024u * 1024u;
                const auto* data = array.Data();
                if (length != expectedLength || data == nullptr || data[0] != 255 || data[length - 1] != 128)
                {
                    ADD_FAILURE() << "Large typed array contents were not preserved.";
                }
            }
            else
            {
                ADD_FAILURE() << "nativeCheckArray expected a single Uint8Array argument.";
            }

            promise.set_value(length);
        }, "nativeCheckArray");

        env.Global().Set("nativeCheckArray", fn);
    });

    Eval(
        "const size = 10 * 1024 * 1024;"
        "const array = new Uint8Array(size);"
        "array[0] = 255;"
        "array[size - 1] = 128;"
        "nativeCheckArray(array);");

    auto future = promise.get_future();
    EXPECT_EQ(Await(future), 10u * 1024u * 1024u);
}

TEST_F(EngineCompatTest, WeakCollections)
{
    std::promise<std::pair<bool, bool>> promise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            std::pair<bool, bool> result{false, false};
            if (info.Length() == 2 && info[0].IsBoolean() && info[1].IsBoolean())
            {
                result.first = info[0].As<Napi::Boolean>().Value();
                result.second = info[1].As<Napi::Boolean>().Value();
            }
            else
            {
                ADD_FAILURE() << "nativeCheckWeakCollections expected two boolean arguments.";
            }

            promise.set_value(result);
        }, "nativeCheckWeakCollections");

        env.Global().Set("nativeCheckWeakCollections", fn);
    });

    Eval(
        "const wm = new WeakMap();"
        "const ws = new WeakSet();"
        "const obj1 = { id: 1 };"
        "const obj2 = { id: 2 };"
        "wm.set(obj1, 'value1');"
        "ws.add(obj2);"
        "nativeCheckWeakCollections(wm.has(obj1), ws.has(obj2));");

    auto future = promise.get_future();
    auto [hasMap, hasSet] = Await(future);
    EXPECT_TRUE(hasMap);
    EXPECT_TRUE(hasSet);
}

TEST_F(EngineCompatTest, ProxyAndReflect)
{
    std::promise<std::tuple<int32_t, int32_t, int32_t>> promise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            std::tuple<int32_t, int32_t, int32_t> result{0, 0, 0};
            if (info.Length() == 3 && info[0].IsNumber() && info[1].IsNumber() && info[2].IsNumber())
            {
                result = {
                    info[0].As<Napi::Number>().Int32Value(),
                    info[1].As<Napi::Number>().Int32Value(),
                    info[2].As<Napi::Number>().Int32Value()
                };
            }
            else
            {
                ADD_FAILURE() << "nativeCheckProxyResults expected three numeric arguments.";
            }

            promise.set_value(result);
        }, "nativeCheckProxyResults");

        env.Global().Set("nativeCheckProxyResults", fn);
    });

    Eval(
        "const target = { value: 42 };"
        "const handler = {"
        "    get(target, prop) {"
        "        if (prop === 'double') {"
        "            return target.value * 2;"
        "        }"
        "        return Reflect.get(target, prop);"
        "    }"
        "};"
        "const proxy = new Proxy(target, handler);"
        "nativeCheckProxyResults(proxy.value, proxy.double, Reflect.get(target, 'value'));");

    auto future = promise.get_future();
    auto [value, doubled, reflectValue] = Await(future);
    EXPECT_EQ(value, 42);
    EXPECT_EQ(doubled, 84);
    EXPECT_EQ(reflectValue, 42);
}

TEST_F(EngineCompatTest, AsyncIteration)
{
    struct Result
    {
        bool success{};
        uint32_t sum{};
        uint32_t count{};
        std::string error;
    };

    std::promise<Result> promise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto successFn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            Result result{};
            result.success = true;
            result.sum = info[0].As<Napi::Number>().Uint32Value();
            result.count = info[1].As<Napi::Number>().Uint32Value();
            promise.set_value(std::move(result));
        }, "nativeAsyncIterationSuccess");

        auto failureFn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            Result result{};
            result.success = false;
            result.error = info[0].As<Napi::String>().Utf8Value();
            promise.set_value(std::move(result));
        }, "nativeAsyncIterationFailure");

        env.Global().Set("nativeAsyncIterationSuccess", successFn);
        env.Global().Set("nativeAsyncIterationFailure", failureFn);
    });

    Eval(
        "(async function(){"
        "    async function* asyncGenerator(){ yield 1; yield 2; yield 3; }"
        "    const values = [];"
        "    for await (const value of asyncGenerator()){ values.push(value); }"
        "    const sum = values.reduce((acc, curr) => acc + curr, 0);"
        "    nativeAsyncIterationSuccess(sum, values.length);"
        "})().catch(e => nativeAsyncIterationFailure(String(e)));");

    auto future = promise.get_future();
    auto result = Await(future, std::chrono::milliseconds{10000});
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.count, 3u);
    EXPECT_EQ(result.sum, 6u);
}

TEST_F(EngineCompatTest, BigIntRoundtrip)
{
#if NAPI_VERSION < 6
    GTEST_SKIP() << "BigInt support requires N-API version > 5.";
#else
    struct Result
    {
        bool available{};
        uint64_t base{};
        uint64_t increment{};
        uint64_t sum{};
        bool baseLossless{};
        bool incrementLossless{};
        bool sumLossless{};
    };

    std::promise<Result> promise;

    Runtime().Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            Result result{};
            result.available = info[0].As<Napi::Boolean>().Value();
            if (result.available)
            {
                bool lossless = false;
                result.base = info[1].As<Napi::BigInt>().Uint64Value(&lossless);
                result.baseLossless = lossless;
                result.increment = info[2].As<Napi::BigInt>().Uint64Value(&lossless);
                result.incrementLossless = lossless;
                result.sum = info[3].As<Napi::BigInt>().Uint64Value(&lossless);
                result.sumLossless = lossless;
            }
            promise.set_value(std::move(result));
        }, "nativeCheckBigInt");

        env.Global().Set("nativeCheckBigInt", fn);
    });

    Eval(
        "if (typeof BigInt === 'undefined') {"
        "    nativeCheckBigInt(false);"
        "} else {"
        "    const base = BigInt(Number.MAX_SAFE_INTEGER);"
        "    const increment = BigInt(1);"
        "    const sum = base + increment;"
        "    nativeCheckBigInt(true, base, increment, sum);"
        "}");

    auto future = promise.get_future();
    auto result = Await(future);
    if (!result.available)
    {
        GTEST_SKIP() << "BigInt not supported in this engine.";
    }

    EXPECT_TRUE(result.baseLossless);
    EXPECT_TRUE(result.incrementLossless);
    EXPECT_TRUE(result.sumLossless);
    EXPECT_GT(result.sum, result.base);
    EXPECT_EQ(result.sum, result.base + result.increment);
#endif
}

TEST_F(EngineCompatTest, GlobalThisRoundtrip)
{
#ifdef _WIN32
    GTEST_SKIP() << "GlobalThis roundtrip test is not supported on Windows builds.";
#else
    std::promise<bool> promise;
    const std::string expectedUtf8 = u8"こんにちは世界🌐";

    Runtime().Dispatch([&](Napi::Env env) {
        auto persistentGlobal = Napi::Persistent(env.Global());
        persistentGlobal.SuppressDestruct();
        auto globalRef = std::make_shared<Napi::ObjectReference>(std::move(persistentGlobal));

        globalRef->Value().Set("nativeUnicodeValue", Napi::String::New(env, expectedUtf8));

        auto fn = Napi::Function::New(env, [globalRef, expectedUtf8, &promise](const Napi::CallbackInfo& info) {
            bool matchesGlobal = false;
            std::string unicode;
            if (info.Length() > 0 && info[0].IsObject())
            {
                matchesGlobal = info[0].As<Napi::Object>().StrictEquals(globalRef->Value());
            }
            if (info.Length() > 1 && info[1].IsString())
            {
                unicode = info[1].As<Napi::String>().Utf8Value();
            }

            EXPECT_TRUE(matchesGlobal);
            EXPECT_EQ(unicode, expectedUtf8);

            promise.set_value(matchesGlobal && unicode == expectedUtf8);
        }, "nativeCheckGlobalThis");

        env.Global().Set("nativeCheckGlobalThis", fn);
        env.Global().Set("nativeGlobalFromCpp", globalRef->Value());
    });

    Eval(
        "const resolvedGlobal = (function(){"
        "  if (typeof globalThis !== 'undefined') return globalThis;"
        "  try { return Function('return this')(); } catch (_) { return nativeGlobalFromCpp; }"
        "})();"
        "const unicodeRoundtrip = resolvedGlobal.nativeUnicodeValue;"
        "nativeCheckGlobalThis(resolvedGlobal, unicodeRoundtrip);");

    auto future = promise.get_future();
    EXPECT_TRUE(Await(future));
#endif
}
