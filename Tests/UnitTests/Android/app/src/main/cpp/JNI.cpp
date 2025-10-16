#include <jni.h>
#include <android/log.h>
#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>
#include <AndroidExtensions/StdoutLogger.h>
#include "Babylon/DebugTrace.h"
#include <Shared/Shared.h>

extern "C" JNIEXPORT jint JNICALL
Java_com_jsruntimehost_unittests_Native_javaScriptTests(JNIEnv* env, jclass clazz, jobject context) {
    JavaVM* javaVM{};
    if (env->GetJavaVM(&javaVM) != JNI_OK)
    {
        throw std::runtime_error{"Failed to get Java VM"};
    }

    jclass webSocketClass{env->FindClass("com/jsruntimehost/unittests/WebSocket")};
    java::websocket::WebSocketClient::InitializeJavaWebSocketClass(webSocketClass, env);

    // StdoutLogger::Start() is now idempotent and safe to call multiple times
    android::StdoutLogger::Start();

    android::global::Initialize(javaVM, context);

    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { __android_log_print(ANDROID_LOG_INFO, "JsRuntimeHost", "%s", trace); });

    auto testResult = RunTests();

    // android::StdoutLogger::Stop();

    java::websocket::WebSocketClient::DestructJavaWebSocketClass(env);
    return testResult;
}
