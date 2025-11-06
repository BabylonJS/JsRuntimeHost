#include "Shared.h"
#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/AbortController.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/WebSocket.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/Blob.h>
#include <gtest/gtest.h>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <fstream>
#include <filesystem>
#include <system_error>

#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>

#include "../../NodeApi/node_lite.h"
#include "../../NodeApi/test_main.h"
#endif

namespace
{
#if defined(__ANDROID__)
    namespace
    {
        using namespace std::filesystem;

        void CopyAssetsRecursive(AAssetManager* manager, const std::string& asset_path, const path& destination)
        {
            AAssetDir* dir = AAssetManager_openDir(manager, asset_path.c_str());
            if (dir == nullptr)
            {
                return;
            }

            const char* filename = nullptr;
            while ((filename = AAssetDir_getNextFileName(dir)) != nullptr)
            {
                std::string child_asset = asset_path.empty() ? filename : asset_path + "/" + filename;
                AAsset* asset = AAssetManager_open(manager, child_asset.c_str(), AASSET_MODE_STREAMING);
                if (asset != nullptr)
                {
                    create_directories(destination);
                    std::ofstream output(destination / filename, std::ios::binary);
                    char buffer[4096];
                    int read = 0;
                    while ((read = AAsset_read(asset, buffer, sizeof(buffer))) > 0)
                    {
                        output.write(buffer, read);
                    }
                    AAsset_close(asset);
                }
                else
                {
                    CopyAssetsRecursive(manager, child_asset, destination / filename);
                }
            }

            AAssetDir_close(dir);
        }

        path GetFilesDir()
        {
            JNIEnv* env = android::global::GetEnvForCurrentThread();
            jobject context = android::global::GetAppContext();
            jclass contextClass = env->GetObjectClass(context);
            jmethodID getFilesDir = env->GetMethodID(contextClass, "getFilesDir", "()Ljava/io/File;");
            jobject filesDir = env->CallObjectMethod(context, getFilesDir);
            env->DeleteLocalRef(contextClass);

            jclass fileClass = env->GetObjectClass(filesDir);
            jmethodID getAbsolutePath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
            jstring pathString = static_cast<jstring>(env->CallObjectMethod(filesDir, getAbsolutePath));
            env->DeleteLocalRef(fileClass);

            const char* rawPath = env->GetStringUTFChars(pathString, nullptr);
            path resultPath{rawPath};
            env->ReleaseStringUTFChars(pathString, rawPath);
            env->DeleteLocalRef(pathString);
            env->DeleteLocalRef(filesDir);

            return resultPath;
        }

        std::unordered_set<std::string> ParseNativeSuiteList()
        {
            std::unordered_set<std::string> suites;
#ifdef NODE_API_AVAILABLE_NATIVE_TESTS
            std::stringstream stream(NODE_API_AVAILABLE_NATIVE_TESTS);
            std::string entry;
            while (std::getline(stream, entry, ','))
            {
                if (!entry.empty())
                {
                    suites.insert(entry);
                }
            }
#endif
            return suites;
        }

        std::optional<path>& OverrideBaseDir()
        {
            static std::optional<path> baseDirOverride{};
            return baseDirOverride;
        }

        AAssetManager*& OverrideAssetManager()
        {
            static AAssetManager* assetManager{};
            return assetManager;
        }

        void ConfigureNodeApiTests()
        {
            static std::once_flag onceFlag;
            std::call_once(onceFlag, []() {
                path baseDir;
                if (OverrideBaseDir())
                {
                    baseDir = *OverrideBaseDir();
                }
                else
                {
                    baseDir = GetFilesDir() / "node_api_tests";
                }
                std::error_code ec;
                std::filesystem::remove_all(baseDir, ec);
                std::filesystem::create_directories(baseDir);

                AAssetManager* assetManagerNative = OverrideAssetManager();
                if (assetManagerNative == nullptr)
                {
                    auto assetManagerWrapper = android::global::GetAppContext().getAssets();
                    assetManagerNative = assetManagerWrapper;
                }

                if (assetManagerNative != nullptr)
                {
                    CopyAssetsRecursive(assetManagerNative, "NodeApi/test", baseDir);
                }

                node_api_tests::NodeApiTestConfig config{};
                config.js_root = baseDir;
                config.run_script = [baseDir](const path& script) {
                    node_api_tests::NodeLiteRuntime::Callbacks callbacks;
                    callbacks.stdout_callback = [](const std::string& message) {
                        __android_log_write(ANDROID_LOG_INFO, "NodeApiTests", message.c_str());
                    };
                    callbacks.stderr_callback = [](const std::string& message) {
                        __android_log_write(ANDROID_LOG_ERROR, "NodeApiTests", message.c_str());
                    };
                    return node_api_tests::RunNodeLiteScript(baseDir, script, std::move(callbacks));
                };
                config.enabled_native_suites = ParseNativeSuiteList();

                node_api_tests::InitializeNodeApiTests(config);
            });
        }
    }
#endif

    const char* EnumToString(Babylon::Polyfills::Console::LogLevel logLevel)
    {
        switch (logLevel)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "error";
        }

        return "unknown";
    }
}

TEST(JavaScript, All)
{
    // Change this to true to wait for the JavaScript debugger to attach (only applies to V8)
    constexpr const bool waitForDebugger = false;

    std::promise<int32_t> exitCodePromise;

    Babylon::AppRuntime::Options options{};

    options.UnhandledExceptionHandler = [&exitCodePromise](const Napi::Error& error) {
        std::cerr << "[Uncaught Error] " << Napi::GetErrorString(error) << std::endl;
        std::cerr.flush();

        exitCodePromise.set_value(-1);
    };

    if (waitForDebugger)
    {
        std::cout << "Waiting for debugger..." << std::endl;
        options.WaitForDebugger = true;
    }

    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&exitCodePromise](Napi::Env env) mutable {
        Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::cout << "[" << EnumToString(logLevel) << "] " << message << std::endl;
            std::cout.flush();
        });

        Babylon::Polyfills::AbortController::Initialize(env);
        Babylon::Polyfills::Scheduling::Initialize(env);
        Babylon::Polyfills::URL::Initialize(env);
        Babylon::Polyfills::WebSocket::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);

        auto setExitCodeCallback = Napi::Function::New(
            env, [&exitCodePromise](const Napi::CallbackInfo& info) {
                Napi::Env env = info.Env();
                exitCodePromise.set_value(info[0].As<Napi::Number>().Int32Value());
            },
            "setExitCode");
        env.Global().Set("setExitCode", setExitCodeCallback);

        env.Global().Set("hostPlatform", Napi::Value::From(env, JSRUNTIMEHOST_PLATFORM));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("location = { href: '' };", ""); // Required for Mocha.js as we do not have a location
    loader.LoadScript("app:///Scripts/tests.js");

    auto exitCode{exitCodePromise.get_future().get()};

    EXPECT_EQ(exitCode, 0);
}

TEST(Console, Log)
{
    Babylon::AppRuntime runtime{};

    runtime.Dispatch([](Napi::Env env) mutable {
        Babylon::Polyfills::Console::Initialize(env, [](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            const char* test = "foo bar";
            if (strcmp(message, test) != 0)
            {
                std::cout << "Expected: " << test << std::endl;
                std::cout << "Received: " << message << std::endl;
                std::cout.flush();
                ADD_FAILURE();
            }
        });
    });

    std::promise<void> done;

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("console.log('foo', 'bar')", "");
    loader.Dispatch([&done](auto) {
        done.set_value();
    });

    done.get_future().get();
}

int RunTests()
{
#if defined(__ANDROID__)
    ConfigureNodeApiTests();
#endif
    testing::InitGoogleTest();
#if defined(__ANDROID__)
    node_api_tests::RegisterNodeApiTests();
#endif
    return RUN_ALL_TESTS();
}
#if defined(__ANDROID__)
void SetNodeApiTestEnvironment(AAssetManager* assetManager, const std::filesystem::path& baseDir)
{
    OverrideAssetManager() = assetManager;
    OverrideBaseDir() = baseDir;
}
#endif
