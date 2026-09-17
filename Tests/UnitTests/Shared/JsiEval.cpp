#include <napi/env.h>
#include <jsi/decorator.h>
#include <V8JsiRuntime.h>
#include <ScriptStore.h>
#include <gtest/gtest.h>
#include <utility>

namespace
{
    // The pinned V8JSI adapter reconstructs exceptions. Inject the original JSError
    // value to exercise our conversion itself, including its primitive fallback.
    class ThrowingRuntime : public facebook::jsi::RuntimeDecorator<>
    {
    public:
        ThrowingRuntime(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& value, bool nativeException = false)
            : RuntimeDecorator{runtime}
            , m_value{runtime, value}
            , m_nativeException{nativeException}
        {
        }

        facebook::jsi::Value evaluateJavaScript(const std::shared_ptr<const facebook::jsi::Buffer>& buffer, const std::string& sourceURL) override
        {
            if (std::exchange(m_throw, false))
            {
                if (m_nativeException)
                {
                    throw facebook::jsi::JSINativeException{"native eval failure"};
                }
                throw facebook::jsi::JSError{plain(), facebook::jsi::Value{plain(), m_value}};
            }
            return plain().evaluateJavaScript(buffer, sourceURL);
        }

    private:
        facebook::jsi::Value m_value;
        bool m_nativeException;
        bool m_throw{true};
    };
}

TEST(JsiEval, PreservesJSErrorObjectIdentity)
{
    const auto runtime = v8runtime::makeV8Runtime({});
    for (const char* source : {"new Error('boom')", "({message: 'boom', sentinel: 42})"})
    {
        SCOPED_TRACE(source);
        const auto original = runtime->evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "original.js");
        ThrowingRuntime throwingRuntime{*runtime, original};
        napi_env__ env{throwingRuntime};
        bool caught{false};
        try
        {
            Napi::Eval(&env, "", "eval-object.js");
        }
        catch (const Napi::Error& error)
        {
            caught = true;
            EXPECT_EQ(error.Message(), "boom");
            EXPECT_TRUE(error.Value().StrictEquals(Napi::Value{&env, facebook::jsi::Value{*runtime, original}}));
        }
        EXPECT_TRUE(caught);
        EXPECT_EQ(Napi::Eval(&env, "1 + 1", "recovery.js").As<Napi::Number>().Int32Value(), 2);
    }
}

TEST(JsiEval, WrapsJSErrorPrimitives)
{
    const auto runtime = v8runtime::makeV8Runtime({});
    for (const char* source : {"'primitive boom'", "42", "true", "null", "undefined", "Symbol('boom')"})
    {
        SCOPED_TRACE(source);
        const auto original = runtime->evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "original.js");
        ASSERT_FALSE(original.isObject());
        ThrowingRuntime throwingRuntime{*runtime, original};
        napi_env__ env{throwingRuntime};
        bool caught{false};
        try
        {
            Napi::Eval(&env, "", "eval-primitive.js");
        }
        catch (const Napi::Error& error)
        {
            caught = true;
            EXPECT_TRUE(error.Value().IsObject());
            EXPECT_FALSE(error.Message().empty());
        }
        EXPECT_TRUE(caught);
        EXPECT_EQ(Napi::Eval(&env, "1 + 1", "recovery.js").As<Napi::Number>().Int32Value(), 2);
    }
}

TEST(JsiEval, ConvertsNativeExceptions)
{
    const auto runtime = v8runtime::makeV8Runtime({});
    ThrowingRuntime throwingRuntime{*runtime, facebook::jsi::Value{}, true};
    napi_env__ env{throwingRuntime};
    bool caught{false};
    try
    {
        Napi::Eval(&env, "", "eval-native.js");
    }
    catch (const Napi::Error& error)
    {
        caught = true;
        EXPECT_EQ(error.Message(), "native eval failure");
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(Napi::Eval(&env, "1 + 1", "recovery.js").As<Napi::Number>().Int32Value(), 2);
}
