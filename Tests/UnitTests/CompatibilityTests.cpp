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

namespace
{
    class EngineCompatTest : public ::testing::Test
    {
    protected:
        Babylon::AppRuntime Runtime;
        std::unique_ptr<Babylon::ScriptLoader> Loader;

        void SetUp() override
        {
            Runtime.Dispatch([](Napi::Env env) {
                Babylon::Polyfills::Console::Initialize(env, [](const char*, Babylon::Polyfills::Console::LogLevel) {});
                Babylon::Polyfills::AbortController::Initialize(env);
                Babylon::Polyfills::Scheduling::Initialize(env);
                Babylon::Polyfills::URL::Initialize(env);
                Babylon::Polyfills::Blob::Initialize(env);
            });

            Loader = std::make_unique<Babylon::ScriptLoader>(Runtime);
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
            Loader->Eval(script.c_str(), "engine-compat");
        }
    };
}

TEST_F(EngineCompatTest, LargeStringRoundtrip)
{
    std::promise<size_t> lengthPromise;

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&lengthPromise](const Napi::CallbackInfo& info) {
            ASSERT_GE(info.Length(), 1);
            ASSERT_TRUE(info[0].IsString());
            const auto value = info[0].As<Napi::String>().Utf8Value();
            EXPECT_EQ(value.size(), 1'000'000u);
            if (!value.empty())
            {
                EXPECT_EQ(value.front(), 'x');
                EXPECT_EQ(value.back(), 'x');
            }
            lengthPromise.set_value(value.size());
        }, "nativeCheckLargeString");

        env.Global().Set("nativeCheckLargeString", fn);
    });

    Eval("const s = 'x'.repeat(1_000_000); nativeCheckLargeString(s);");

    auto future = lengthPromise.get_future();
    EXPECT_EQ(Await(future), 1'000'000u);
}

TEST_F(EngineCompatTest, SymbolCrossing)
{
    std::promise<bool> donePromise;

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&donePromise](const Napi::CallbackInfo& info) {
            ASSERT_EQ(info.Length(), 4);
            ASSERT_TRUE(info[0].IsSymbol());
            ASSERT_TRUE(info[1].IsSymbol());
            ASSERT_TRUE(info[2].IsSymbol());
            ASSERT_TRUE(info[3].IsSymbol());

            const auto sym1 = info[0].As<Napi::Symbol>();
            const auto sym2 = info[1].As<Napi::Symbol>();
            const auto sym3 = info[2].As<Napi::Symbol>();
            const auto sym4 = info[3].As<Napi::Symbol>();

            EXPECT_FALSE(sym1.StrictEquals(sym2));
            EXPECT_TRUE(sym3.StrictEquals(sym4));

            auto sym3String = sym3.ToString().Utf8Value();
            EXPECT_NE(sym3String.find("global"), std::string::npos);

            donePromise.set_value(true);
        }, "nativeCheckSymbols");

        env.Global().Set("nativeCheckSymbols", fn);
    });

    Eval(
        "const sym1 = Symbol('test');"
        "const sym2 = Symbol('test');"
        "const sym3 = Symbol.for('global');"
        "const sym4 = Symbol.for('global');"
        "nativeCheckSymbols(sym1, sym2, sym3, sym4);");

    auto future = donePromise.get_future();
    EXPECT_TRUE(Await(future));
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

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&resultPromise](const Napi::CallbackInfo& info) {
            ASSERT_EQ(info.Length(), 4);
            ASSERT_TRUE(info[0].IsString());
            ASSERT_TRUE(info[1].IsNumber());
            ASSERT_TRUE(info[2].IsNumber());
            ASSERT_TRUE(info[3].IsArray());

            Result result{};
            result.value = info[0].As<Napi::String>().Utf16Value();
            result.high = info[1].As<Napi::Number>().Uint32Value();
            result.low = info[2].As<Napi::Number>().Uint32Value();

            auto array = info[3].As<Napi::Array>();
            for (uint32_t i = 0; i < array.Length(); ++i)
            {
                result.spread.emplace_back(array.Get(i).As<Napi::String>().Utf8Value());
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
    ASSERT_EQ(result.spread.size(), 3u);
    EXPECT_EQ(result.spread[0], "\xF0\x9F\x98\x80"); // 😀
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

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&resultPromise](const Napi::CallbackInfo& info) {
            ASSERT_EQ(info.Length(), 5);

            Result result{};
            result.bmp = info[0].As<Napi::String>().Utf8Value();
            result.supplementary = info[1].As<Napi::String>().Utf16Value();
            result.combining = info[2].As<Napi::String>().Utf8Value();
            result.normalizedNfc = info[3].As<Napi::String>().Utf8Value();
            result.normalizedNfd = info[4].As<Napi::String>().Utf8Value();

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
    EXPECT_TRUE(result.normalizedNfd.size() == 1u || result.normalizedNfd.size() == 2u);
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

    Runtime.Dispatch([&](Napi::Env env) {
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

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            ASSERT_EQ(info.Length(), 1);
            ASSERT_TRUE(info[0].IsTypedArray());

            auto array = info[0].As<Napi::Uint8Array>();
            EXPECT_EQ(array.ElementLength(), 10u * 1024u * 1024u);
            EXPECT_EQ(array[0], 255);
            EXPECT_EQ(array[array.ElementLength() - 1], 128);

            promise.set_value(array.ElementLength());
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

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            ASSERT_EQ(info.Length(), 2);
            promise.set_value({info[0].As<Napi::Boolean>().Value(), info[1].As<Napi::Boolean>().Value()});
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

    Runtime.Dispatch([&](Napi::Env env) {
        auto fn = Napi::Function::New(env, [&promise](const Napi::CallbackInfo& info) {
            ASSERT_EQ(info.Length(), 3);
            promise.set_value(
                {
                    info[0].As<Napi::Number>().Int32Value(),
                    info[1].As<Napi::Number>().Int32Value(),
                    info[2].As<Napi::Number>().Int32Value(),
                });
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

    Runtime.Dispatch([&](Napi::Env env) {
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
#if NAPI_VERSION > 5
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

    Runtime.Dispatch([&](Napi::Env env) {
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
#else
    GTEST_SKIP() << "BigInt support requires N-API version > 5.";
#endif
}
TEST_F(EngineCompatTest, GlobalThisRoundtrip)
{
#if !defined(_WIN32)
    std::promise<bool> promise;

    Runtime.Dispatch([&](Napi::Env env) {
        auto persistentGlobal = Napi::Persistent(env.Global());
        persistentGlobal.SuppressDestruct();
        auto globalRef = std::make_shared<Napi::ObjectReference>(std::move(persistentGlobal));

        auto fn = Napi::Function::New(env, [globalRef, &promise](const Napi::CallbackInfo& info) {
            bool matchesGlobal = false;
            if (info.Length() > 0 && info[0].IsObject())
            {
                matchesGlobal = info[0].As<Napi::Object>().StrictEquals(globalRef->Value());
            }
            promise.set_value(matchesGlobal);
        }, "nativeCheckGlobalThis");

        env.Global().Set("nativeCheckGlobalThis", fn);
        env.Global().Set("nativeGlobalFromCpp", globalRef->Value());
    });

    Eval(
        "const resolvedGlobal = (function(){"
        "  if (typeof globalThis !== 'undefined') return globalThis;"
        "  try { return Function('return this')(); } catch (_) { return nativeGlobalFromCpp; }"
        "})();"
        "nativeCheckGlobalThis(resolvedGlobal);");

    auto future = promise.get_future();
    EXPECT_TRUE(Await(future));
#else
    GTEST_SKIP() << "Chakra does not support globalThis";
#endif
}

