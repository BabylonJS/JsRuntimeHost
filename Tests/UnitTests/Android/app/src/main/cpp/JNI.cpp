#include <jni.h>
#include <Android/log.h>
#include <AndroidExtensions/Globals.h>
#include <Shared/Shared.h>

extern "C" JNIEXPORT jint JNICALL
Java_com_jsruntimehost_unittests_Native_javaScriptTests(JNIEnv* env, jclass clazz, jobject context) {
    JavaVM* javaVM{};
    if (env->GetJavaVM(&javaVM) != JNI_OK)
    {
        throw std::runtime_error{"Failed to get Java VM"};
    }
    // test code
    jclass webSocketClass{env->FindClass("com/jsruntimehost/unittests/WebSocket")};
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
    }

    jmethodID mid = env->GetMethodID(webSocketClass, "<init>", "()V");
    jobject obj = env->NewObject(webSocketClass, mid);

    jmethodID method{env->GetMethodID(webSocketClass, "liyaanMethod", "()V")};
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
    }
    env->CallVoidMethod(obj, method);

    android::global::Initialize(javaVM, context);

    auto consoleCallback = [](const char* message, Babylon::Polyfills::Console::LogLevel level)
    {
        switch (level)
        {
        case Babylon::Polyfills::Console::LogLevel::Log:
            __android_log_write(ANDROID_LOG_INFO, "JsRuntimeHost", message);
            break;
        case Babylon::Polyfills::Console::LogLevel::Warn:
            __android_log_write(ANDROID_LOG_WARN, "JsRuntimeHost", message);
            break;
        case Babylon::Polyfills::Console::LogLevel::Error:
            __android_log_write(ANDROID_LOG_ERROR, "JsRuntimeHost", message);
            break;
        }
    };

    return RunTests(consoleCallback);
}
