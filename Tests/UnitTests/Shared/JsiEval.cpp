#include "EvalInternal.h"
#include <V8JsiRuntime.h>
#include <ScriptStore.h>
#include <gtest/gtest.h>

// The pinned V8JSI adapter reconstructs thrown exceptions. Exercise the same
// conversion used by Eval directly to retain the original JSError value.
TEST(JsiEval, PreservesJSErrorObjectIdentity)
{
    const auto runtime = v8runtime::makeV8Runtime({});
    napi_env__ env{*runtime};
    for (const char* source : {"new Error('boom')", "({message: 'boom', sentinel: 42})"})
    {
        SCOPED_TRACE(source);
        const auto original = runtime->evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "original.js");
        const facebook::jsi::JSError exception{*runtime, facebook::jsi::Value{*runtime, original}};
        const auto error = Napi::Internal::ConvertEvalException(&env, exception);
        EXPECT_EQ(error.Message(), "boom");
        EXPECT_TRUE(error.Value().StrictEquals(Napi::Value{&env, facebook::jsi::Value{*runtime, original}}));
        EXPECT_EQ(Napi::Eval(&env, "1 + 1", "recovery.js").As<Napi::Number>().Int32Value(), 2);
    }
}

TEST(JsiEval, WrapsJSErrorPrimitives)
{
    const auto runtime = v8runtime::makeV8Runtime({});
    napi_env__ env{*runtime};
    for (const char* source : {"'primitive boom'", "42", "true", "null", "undefined", "Symbol('boom')"})
    {
        SCOPED_TRACE(source);
        const auto original = runtime->evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "original.js");
        ASSERT_FALSE(original.isObject());
        const facebook::jsi::JSError exception{*runtime, facebook::jsi::Value{*runtime, original}};
        const auto error = Napi::Internal::ConvertEvalException(&env, exception);
        EXPECT_TRUE(error.Value().IsObject());
        EXPECT_FALSE(error.Message().empty());
        EXPECT_EQ(Napi::Eval(&env, "1 + 1", "recovery.js").As<Napi::Number>().Int32Value(), 2);
    }
}

TEST(JsiEval, ConvertsNativeExceptions)
{
    const auto runtime = v8runtime::makeV8Runtime({});
    napi_env__ env{*runtime};
    const facebook::jsi::JSINativeException exception{"native eval failure"};
    const auto error = Napi::Internal::ConvertEvalException(&env, exception);
    EXPECT_EQ(error.Message(), "native eval failure");
    EXPECT_EQ(Napi::Eval(&env, "1 + 1", "recovery.js").As<Napi::Number>().Int32Value(), 2);
}
