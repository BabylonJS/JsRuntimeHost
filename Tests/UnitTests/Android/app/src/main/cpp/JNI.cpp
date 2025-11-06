#include <jni.h>
#include <Android/log.h>
#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>
#include <android/asset_manager_jni.h>
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

    jclass contextClass = env->GetObjectClass(context);
    jmethodID getApplicationContext = env->GetMethodID(contextClass, "getApplicationContext", "()Landroid/content/Context;");
    jobject applicationContext = env->CallObjectMethod(context, getApplicationContext);
    env->DeleteLocalRef(contextClass);

    jclass appContextClass = env->GetObjectClass(applicationContext);
    jmethodID getAssets = env->GetMethodID(appContextClass, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assetManagerObj = env->CallObjectMethod(applicationContext, getAssets);
    env->DeleteLocalRef(appContextClass);

    android::global::Initialize(javaVM, applicationContext, assetManagerObj);

    if (assetManagerObj != nullptr)
    {
        env->DeleteLocalRef(assetManagerObj);
    }

    env->DeleteLocalRef(applicationContext);

    Babylon::DebugTrace::EnableDebugTrace(true);
    Babylon::DebugTrace::SetTraceOutput([](const char* trace) { printf("%s\n", trace); fflush(stdout); });

    auto testResult = RunTests();

    java::websocket::WebSocketClient::DestructJavaWebSocketClass(env);
    return testResult;
}
