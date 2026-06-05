#include "Shared.h"
#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/AbortController.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Performance.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/WebSocket.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <Babylon/Polyfills/TextEncoder.h>
#include <gtest/gtest.h>
#include <arcana/threading/blocking_concurrent_queue.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

#if defined(__ANDROID__) && defined(NODE_API_AVAILABLE_NATIVE_TESTS)
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
#if defined(__ANDROID__) && defined(NODE_API_AVAILABLE_NATIVE_TESTS)
    namespace
    {
        using namespace std::filesystem;

        void CopyAssetsRecursive(AAssetManager* manager, const std::string& asset_path, const path& destination)
        {
            // The NDK AAssetManager cannot enumerate subdirectories -- AAssetDir_getNextFileName
            // returns files in a single directory only, never nested directories -- so the test
            // tree cannot be discovered at runtime. Instead read a build-time manifest (one
            // relative path per line, produced by the copyNodeApiTests Gradle task) and copy each
            // listed file individually (AAssetManager_open works fine for a known file path).
            std::string manifest_asset = asset_path + "/manifest.txt";
            AAsset* manifest = AAssetManager_open(manager, manifest_asset.c_str(), AASSET_MODE_BUFFER);
            if (manifest == nullptr)
            {
                return;
            }

            off_t manifest_length = AAsset_getLength(manifest);
            std::string manifest_text(static_cast<size_t>(manifest_length), '\0');
            AAsset_read(manifest, manifest_text.data(), manifest_length);
            AAsset_close(manifest);

            std::stringstream manifest_stream(manifest_text);
            std::string relative_path;
            while (std::getline(manifest_stream, relative_path))
            {
                if (!relative_path.empty() && relative_path.back() == '\r')
                {
                    relative_path.pop_back();
                }
                if (relative_path.empty())
                {
                    continue;
                }

                std::string child_asset = asset_path + "/" + relative_path;
                AAsset* asset = AAssetManager_open(manager, child_asset.c_str(), AASSET_MODE_STREAMING);
                if (asset == nullptr)
                {
                    continue;
                }

                path output_path = destination / relative_path;
                create_directories(output_path.parent_path());
                std::ofstream output(output_path, std::ios::binary);
                char buffer[8192];
                int read = 0;
                while ((read = AAsset_read(asset, buffer, sizeof(buffer))) > 0)
                {
                    output.write(buffer, read);
                }
                AAsset_close(asset);
            }
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
                    auto result = node_api_tests::RunNodeLiteScript(baseDir, script, std::move(callbacks));
                    // Surface the in-process failure detail to logcat. The runner keeps the assertion /
                    // exception message + stack in result.std_error; without this it never reaches the
                    // device log, making on-device conformance failures undebuggable.
                    if (result.status != 0) {
                        std::string detail = result.std_error.empty() ? "(no std_error captured)" : result.std_error;
                        __android_log_write(ANDROID_LOG_ERROR, "NodeApiTests",
                            ("[node_lite status=" + std::to_string(result.status) + "] " + detail).c_str());
                    }
                    return result;
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
        Babylon::Polyfills::Console::Initialize(env, [env](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::cout << "[" << EnumToString(logLevel) << "] " << message;
            if (logLevel == Babylon::Polyfills::Console::LogLevel::Error)
            {
                std::string stack = Babylon::Polyfills::Console::CaptureCurrentJsStack(env);
                if (!stack.empty())
                {
                    std::cout << std::endl << stack;
                }
            }
            std::cout << std::endl;
            std::cout.flush();
        });

        Babylon::Polyfills::AbortController::Initialize(env);
        Babylon::Polyfills::Performance::Initialize(env);
        Babylon::Polyfills::Scheduling::Initialize(env);
        Babylon::Polyfills::URL::Initialize(env);
        Babylon::Polyfills::WebSocket::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        Babylon::Polyfills::TextDecoder::Initialize(env);
        Babylon::Polyfills::TextEncoder::Initialize(env);

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

TEST(Console, CaptureCurrentJsStack)
{
    // Regression: Console::CaptureCurrentJsStack must return a non-empty stack when called from
    // within a callback fired by `console.error`, and when called from `console.log` (any frame
    // produced by JS execution).
    Babylon::AppRuntime runtime{};

    std::promise<std::string> errorStackPromise;
    std::promise<std::string> logStackPromise;

    runtime.Dispatch([&errorStackPromise, &logStackPromise](Napi::Env env) mutable {
        Babylon::Polyfills::Console::Initialize(env, [env, &errorStackPromise, &logStackPromise](const char* /*message*/, Babylon::Polyfills::Console::LogLevel logLevel) {
            std::string stack = Babylon::Polyfills::Console::CaptureCurrentJsStack(env);
            if (logLevel == Babylon::Polyfills::Console::LogLevel::Error)
            {
                errorStackPromise.set_value(std::move(stack));
            }
            else if (logLevel == Babylon::Polyfills::Console::LogLevel::Log)
            {
                logStackPromise.set_value(std::move(stack));
            }
        });
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("console.log('log message');", "");
    loader.Eval("function inner() { console.error('error message'); } inner();", "");

    auto errorFuture = errorStackPromise.get_future();
    auto logFuture = logStackPromise.get_future();
    constexpr auto timeout = std::chrono::seconds(30);
    ASSERT_EQ(errorFuture.wait_for(timeout), std::future_status::ready)
        << "console.error callback did not fire within timeout";
    ASSERT_EQ(logFuture.wait_for(timeout), std::future_status::ready)
        << "console.log callback did not fire within timeout";

    std::string errorStack = errorFuture.get();
    std::string logStack = logFuture.get();

    EXPECT_FALSE(errorStack.empty()) << "console.error path must capture a non-empty JS stack";
    EXPECT_FALSE(logStack.empty()) << "console.log path must capture a non-empty JS stack";
}

TEST(AppRuntime, DestroyDoesNotDeadlock)
{
    // Regression test verifying AppRuntime destruction doesn't deadlock.
    // Uses a global arcana hook to sleep while holding the queue mutex
    // before wait(), ensuring the worker is in the vulnerable window
    // when the destructor fires. See #147 for details on the bug and fix.
    //
    // The entire test runs on a separate thread so the gtest thread can
    // detect a deadlock via timeout without hanging the process.
    //
    // Test flow:
    //
    //   Test Thread                    Worker Thread
    //   -----------                    -------------
    //   1. Create AppRuntime           Worker starts, enters blocking_tick
    //      Wait for init to complete
    //   2. Install hook
    //      Dispatch(no-op)             Worker wakes, runs no-op,
    //                                  returns to blocking_tick
    //                                  Hook fires:
    //                                    signal workerInHook
    //                                    sleep 200ms (holding mutex!)
    //   3. workerInHook.wait()
    //      Worker is sleeping in hook
    //   4. ~AppRuntime():
    //          cancel()
    //          Append(no-op):
    //            push() blocks ------> (worker holds mutex)
    //                                  200ms sleep ends
    //                                  wait(lock) releases mutex
    //            push() acquires mutex
    //            pushes, notifies ---> wakes up!
    //            join() waits          drains no-op, cancelled -> exit
    //            join() returns <----- thread exits
    //   5. destroy completes -> PASS

    bool hookSignaled{false};
    std::promise<void> workerInHook;
    std::promise<void> testDone;

    // Run the full lifecycle on a separate thread so the gtest thread
    // can detect a deadlock via timeout.
    std::thread testThread([&]() {
        auto runtime = std::make_unique<Babylon::AppRuntime>();

        // Wait for the runtime to fully initialize. The constructor dispatches
        // CreateForJavaScript which must complete before we install the hook
        // so the worker is idle and ready to enter the hook on the next wait.
        std::promise<void> ready;
        runtime->Dispatch([&ready](Napi::Env) {
            ready.set_value();
        });
        ready.get_future().wait();

        // Install the hook and dispatch a no-op to wake the worker,
        // ensuring it cycles through the hook on its way back to idle.
        arcana::test_hooks::blocking_concurrent_queue::set_before_wait_callback([&]() {
            if (hookSignaled)
            {
                return;
            }
            hookSignaled = true;
            workerInHook.set_value();
            // This sleep is not truly deterministic. Its purpose is to hold the
            // mutex long enough for runtime.reset() (called by the test thread
            // after workerInHook signals) to reach push() while the mutex is
            // still held. When the sleep ends, the worker enters wait() which
            // releases the mutex, allowing push() to acquire it and deliver the
            // wake-up notification. If runtime.reset() hasn't reached push()
            // by the time the sleep ends, the test still passes but doesn't
            // exercise the intended contention window.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        });
        runtime->Dispatch([](Napi::Env) {});

        // Wait for the worker to be in the hook (holding mutex, sleeping)
        workerInHook.get_future().wait();

        // Destroy — if the fix works, the destructor completes.
        // If broken, it deadlocks and the timeout detects it.
        runtime.reset();
        testDone.set_value();
    });

    auto status = testDone.get_future().wait_for(std::chrono::seconds(5));

    arcana::test_hooks::blocking_concurrent_queue::set_before_wait_callback([]() {});

    if (status == std::future_status::timeout)
    {
        testThread.detach();
        FAIL() << "Deadlock detected: AppRuntime destructor did not complete within 5 seconds";
    }

    testThread.join();
}

int RunTests()
{
#if defined(__ANDROID__) && defined(NODE_API_AVAILABLE_NATIVE_TESTS)
    ConfigureNodeApiTests();
#endif
    testing::InitGoogleTest();
#if defined(__ANDROID__) && defined(NODE_API_AVAILABLE_NATIVE_TESTS)
    node_api_tests::RegisterNodeApiTests();
#endif
    return RUN_ALL_TESTS();
}
#if defined(__ANDROID__) && defined(NODE_API_AVAILABLE_NATIVE_TESTS)
void SetNodeApiTestEnvironment(AAssetManager* assetManager, const std::filesystem::path& baseDir)
{
    OverrideAssetManager() = assetManager;
    OverrideBaseDir() = baseDir;
}
#endif
