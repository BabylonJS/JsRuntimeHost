#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/ImportScripts.h>
#include <Babylon/ScriptLoader.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>

TEST(ImportScripts, LoadsOnlyResolvedResources)
{
    std::promise<std::string> result;
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&result](Napi::Env env) {
        Babylon::Polyfills::ImportScripts::Initialize(env, [](const std::string& name) -> std::optional<std::string> {
            if (name == "chunk.js")
            {
                return "self.chunkValue = 42;";
            }
            return std::nullopt;
        });
        env.Global().Set("reportResult", Napi::Function::New(env, [&result](const Napi::CallbackInfo& info) {
            result.set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        var messages = [];
        importScripts("chunk.js", "chunk.js");
        messages.push(String(self.chunkValue));
        try { importScripts("https://example.com/chunk.js"); }
        catch (error) { messages.push(error.message); }
        try { importScripts("../chunk.js"); }
        catch (error) { messages.push(error.message); }
        reportResult(messages.join("|"));
    )", "entry.js");

    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(future.get(),
        "42|Embedded script not found: https://example.com/chunk.js|Embedded script not found: ../chunk.js");
}

TEST(ImportScripts, PropagatesResolverAndEvaluationErrors)
{
    std::promise<std::string> result;
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&result](Napi::Env env) {
        Babylon::Polyfills::ImportScripts::Initialize(env, [](const std::string& name) -> std::optional<std::string> {
            if (name == "broken.js")
            {
                return "throw new Error('chunk failed')";
            }
            throw std::runtime_error{"resource lookup failed"};
        });
        env.Global().Set("reportResult", Napi::Function::New(env, [&result](const Napi::CallbackInfo& info) {
            result.set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        var messages = [];
        try { importScripts("broken.js"); }
        catch (error) { messages.push(error.message); }
        try { importScripts("unavailable.js"); }
        catch (error) { messages.push(error.message); }
        try { importScripts(42); }
        catch (error) { messages.push(error.name); }
        reportResult(messages.join("|"));
    )", "entry.js");

    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(future.get(), "chunk failed|resource lookup failed|TypeError");
}

TEST(ImportScripts, LoadsDeferredChunksInsidePromise)
{
    std::promise<std::string> result;
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&result](Napi::Env env) {
        Babylon::Polyfills::ImportScripts::Initialize(env, [](const std::string& name) -> std::optional<std::string> {
            if (name == "deferred.js")
            {
                return "self.deferredValue = 42;";
            }
            return std::nullopt;
        });
        env.Global().Set("reportResult", Napi::Function::New(env, [&result](const Napi::CallbackInfo& info) {
            result.set_value(info[0].As<Napi::String>().Utf8Value());
        }));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        Promise.resolve()
            .then(function () { importScripts("deferred.js"); return self.deferredValue; })
            .then(function (value) {
                return Promise.resolve().then(function () { importScripts("missing.js"); })
                    .then(function () { reportResult("missing chunk unexpectedly loaded"); },
                          function (error) { reportResult(value + "|" + error.message); });
            });
    )", "entry.js");

    auto future = result.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(future.get(), "42|Embedded script not found: missing.js");
}
