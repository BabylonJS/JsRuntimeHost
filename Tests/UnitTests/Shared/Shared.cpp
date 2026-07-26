#include "Shared.h"
#include <Babylon/AppRuntime.h>
#include <napi/env.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/AbortController.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Performance.h>
#include <Babylon/Polyfills/Scheduling.h>
#include <Babylon/Polyfills/URL.h>
#include <Babylon/Polyfills/WebSocket.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Polyfills/Fetch.h>
#include <Babylon/Polyfills/Blob.h>
#include <Babylon/Polyfills/File.h>
#include <Babylon/Polyfills/IndexedDB.h>
#include <Babylon/Polyfills/TextDecoder.h>
#include <Babylon/Polyfills/TextEncoder.h>
#include <Babylon/Polyfills/Streams.h>
#include <Babylon/Polyfills/Compression.h>
#include <Babylon/Polyfills/Streams.h>
#include <Babylon/Polyfills/Streams.h>
#include <Babylon/Polyfills/Streams.h>
#if defined(JSRUNTIMEHOST_TEST_WORKER)
#include <Babylon/Polyfills/Worker.h>
#endif
#include <gtest/gtest.h>
#include <arcana/threading/blocking_concurrent_queue.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
        Babylon::Polyfills::Streams::Initialize(env);
        Babylon::Polyfills::Blob::Initialize(env);
        Babylon::Polyfills::File::Initialize(env);
        Babylon::Polyfills::TextDecoder::Initialize(env);
        Babylon::Polyfills::TextEncoder::Initialize(env);
        Babylon::Polyfills::Streams::Initialize(env);
        Babylon::Polyfills::Compression::Initialize(env);
        Babylon::Polyfills::Fetch::Initialize(env);
        Babylon::Polyfills::IndexedDB::Initialize(env);

#if defined(JSRUNTIMEHOST_TEST_WORKER)
        Babylon::Polyfills::Worker::Options workerOptions{};
        workerOptions.ScriptRoot = std::filesystem::current_path().string();
        Babylon::Polyfills::Worker::Initialize(env, std::move(workerOptions));
#endif

        auto setExitCodeCallback = Napi::Function::New(
            env, [&exitCodePromise](const Napi::CallbackInfo& info) {
                Napi::Env env = info.Env();
                exitCodePromise.set_value(info[0].As<Napi::Number>().Int32Value());
            },
            "setExitCode");
        env.Global().Set("setExitCode", setExitCodeCallback);

        env.Global().Set("hostPlatform", Napi::Value::From(env, JSRUNTIMEHOST_PLATFORM));
        env.Global().Set("hostEngine", Napi::Value::From(env, NAPI_JAVASCRIPT_ENGINE));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval("location = { href: '' };", ""); // Required for Mocha.js as we do not have a location
    loader.LoadScript("app:///Scripts/tests.js");

    auto exitCode{exitCodePromise.get_future().get()};

    EXPECT_EQ(exitCode, 0);
}

// The unit test host's UnhandledExceptionHandler fails the whole JavaScript
// suite, so a throwing timer callback cannot be exercised from tests.ts. This
// covers it natively instead.
TEST(Scheduling, IntervalSurvivesThrowingCallback)
{
    // Regression: repeating timeouts are re-armed after their callback returns
    // rather than before it runs, so an exception escaping a tick must not
    // silently stop the interval. Browsers keep the interval running and report
    // the error, and that is also what this dispatcher did previously.
    std::promise<int32_t> tickCountPromise;
    std::atomic<int32_t> unhandledErrorCount{0};

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [&unhandledErrorCount](const Napi::Error&) {
        ++unhandledErrorCount;
    };

    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&tickCountPromise](Napi::Env env) {
        Babylon::Polyfills::Scheduling::Initialize(env);

        auto reportTicks = Napi::Function::New(
            env, [&tickCountPromise](const Napi::CallbackInfo& info) {
                tickCountPromise.set_value(info[0].As<Napi::Number>().Int32Value());
            },
            "reportTicks");
        env.Global().Set("reportTicks", reportTicks);
    });

    Babylon::ScriptLoader loader{runtime};
    loader.Eval(R"(
        var ticks = 0;
        var id = setInterval(function () {
            ticks++;
            if (ticks === 3) {
                clearInterval(id);
                reportTicks(ticks);
                return;
            }
            throw new Error('tick failed');
        }, 1);
    )",
        "");

    auto tickCountFuture{tickCountPromise.get_future()};
    ASSERT_EQ(tickCountFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "the interval stopped after a tick threw";
    EXPECT_EQ(tickCountFuture.get(), 3);

    // The first two ticks threw, and those errors must still be surfaced.
    EXPECT_EQ(unhandledErrorCount.load(), 2);
}

TEST(Streams, ReplacesPartialHostSuiteAndIsIdempotent)
{
    std::promise<std::string> done;
    std::atomic_bool completed{};
    const auto complete = [&done, &completed](std::string result) {
        if (!completed.exchange(true))
        {
            done.set_value(std::move(result));
        }
    };

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [&complete](const Napi::Error& error) {
        complete(Napi::GetErrorString(error));
    };
    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&complete](Napi::Env env) {
        auto global = env.Global();
        const auto hostReadableStream = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostReadableStream");
        global.Set("ReadableStream", hostReadableStream);
        global.Set("TransformStream", env.Null());

        Babylon::Polyfills::Streams::Initialize(env);
        const auto installedReadableStream = global.Get("ReadableStream");
        const auto installedTransformStream = global.Get("TransformStream");
        EXPECT_FALSE(installedReadableStream.StrictEquals(hostReadableStream));
        if (!installedReadableStream.IsFunction() || !installedTransformStream.IsFunction())
        {
            complete("Streams::Initialize did not install a complete constructor suite.");
            return;
        }

        const auto transform = installedTransformStream.As<Napi::Function>().New({});
        EXPECT_TRUE(transform.Get("readable").As<Napi::Object>().InstanceOf(installedReadableStream.As<Napi::Function>()));

        Babylon::Polyfills::Streams::Initialize(env);
        EXPECT_TRUE(global.Get("ReadableStream").StrictEquals(installedReadableStream));
        EXPECT_TRUE(global.Get("TransformStream").StrictEquals(installedTransformStream));
        complete({});
    });

    const auto error = done.get_future().get();
    EXPECT_TRUE(error.empty()) << error;
}

TEST(Compression, PreservesHostConstructorsAndIsIdempotent)
{
    Babylon::AppRuntime runtime{};
    std::promise<void> done;

    runtime.Dispatch([&done](Napi::Env env) {
        auto global = env.Global();
        const auto hostCompressionStream = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostCompressionStream");
        global.Set("CompressionStream", hostCompressionStream);

        Babylon::Polyfills::Compression::Initialize(env);
        EXPECT_TRUE(global.Get("CompressionStream").StrictEquals(hostCompressionStream));
        EXPECT_TRUE(global.Get("DecompressionStream").IsFunction());

        const auto installedDecompressionStream = global.Get("DecompressionStream");
        Babylon::Polyfills::Compression::Initialize(env);
        EXPECT_TRUE(global.Get("CompressionStream").StrictEquals(hostCompressionStream));
        EXPECT_TRUE(global.Get("DecompressionStream").StrictEquals(installedDecompressionStream));
        done.set_value();
    });

    done.get_future().get();
}

TEST(IndexedDB, PreservesHostImplementation)
{
    std::promise<void> done;
    Babylon::AppRuntime runtime{};

    runtime.Dispatch([&done](Napi::Env env) {
        auto global = env.Global();
        auto hostIndexedDB = Napi::Object::New(env);
        global.Set("indexedDB", hostIndexedDB);

        Babylon::Polyfills::IndexedDB::Initialize(env);
        EXPECT_TRUE(global.Get("indexedDB").StrictEquals(hostIndexedDB));

        Babylon::Polyfills::IndexedDB::Initialize(env);
        EXPECT_TRUE(global.Get("indexedDB").StrictEquals(hostIndexedDB));
        done.set_value();
    });

    done.get_future().get();
}

TEST(Fetch, PreservesCompleteHostClassesAndIsIdempotent)
{
    std::promise<std::string> done;
    std::atomic_bool completed{};
    const auto complete = [&done, &completed](std::string result) {
        if (!completed.exchange(true))
        {
            done.set_value(std::move(result));
        }
    };

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [&complete](const Napi::Error& error) {
        complete(Napi::GetErrorString(error));
    };
    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&complete](Napi::Env env) {
        auto global = env.Global();
        const auto hostHeaders = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostHeaders");
        const auto hostResponse = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostResponse");
        global.Set("Headers", hostHeaders);
        global.Set("Response", hostResponse);

        Babylon::Polyfills::Fetch::Initialize(env);
        EXPECT_TRUE(global.Get("Headers").StrictEquals(hostHeaders));
        EXPECT_TRUE(global.Get("Response").StrictEquals(hostResponse));

        const auto installedFetch = global.Get("fetch");
        Babylon::Polyfills::Fetch::Initialize(env);
        EXPECT_TRUE(global.Get("Headers").StrictEquals(hostHeaders));
        EXPECT_TRUE(global.Get("Response").StrictEquals(hostResponse));
        EXPECT_TRUE(global.Get("fetch").StrictEquals(installedFetch));
        complete({});
    });

    const auto error = done.get_future().get();
    EXPECT_TRUE(error.empty()) << error;
}

TEST(Fetch, ReplacesPartialOrNullHostClassesAsACompletePair)
{
    std::promise<std::string> done;
    std::atomic_bool completed{};
    const auto complete = [&done, &completed](std::string result) {
        if (!completed.exchange(true))
        {
            done.set_value(std::move(result));
        }
    };

    Babylon::AppRuntime::Options options{};
    options.UnhandledExceptionHandler = [&complete](const Napi::Error& error) {
        complete(Napi::GetErrorString(error));
    };
    Babylon::AppRuntime runtime{options};

    runtime.Dispatch([&complete](Napi::Env env) {
        auto global = env.Global();
        const auto hostHeaders = Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostHeaders");
        global.Set("Headers", hostHeaders);
        global.Set("Response", env.Null());

        Babylon::Polyfills::Fetch::Initialize(env);
        const auto installedHeaders = global.Get("Headers");
        const auto installedResponse = global.Get("Response");
        EXPECT_FALSE(installedHeaders.StrictEquals(hostHeaders));
        if (!installedHeaders.IsFunction() || !installedResponse.IsFunction())
        {
            complete("Fetch::Initialize did not install a complete Headers/Response pair.");
            return;
        }

        const auto response = installedResponse.As<Napi::Function>().New({});
        EXPECT_TRUE(response.Get("headers").As<Napi::Object>().InstanceOf(installedHeaders.As<Napi::Function>()));

        Babylon::Polyfills::Fetch::Initialize(env);
        EXPECT_TRUE(global.Get("Headers").StrictEquals(installedHeaders));
        EXPECT_TRUE(global.Get("Response").StrictEquals(installedResponse));
        complete({});
    });

    const auto error = done.get_future().get();
    EXPECT_TRUE(error.empty()) << error;
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

// N-API results must not depend on anything reachable from script. The JavaScriptCore backend has no
// BigInt C API below macOS 15 / iOS 18 / visionOS 2, and none at all on Android, so it reaches BigInt
// through JS intrinsics; those are captured at env init (like Function.prototype.call) precisely so a
// page that replaces `BigInt`, `BigInt.asIntN/asUintN` or `BigInt.prototype.toString` cannot steer an
// addon's napi_*_bigint_* calls through its own code. Before that, every one of these entry points
// re-resolved the name on the live global object, and the create path evaluated a `BigInt("...")`
// source string -- so the patches below each returned an attacker-chosen value.
#if !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI)
TEST(NodeApi, BigIntIgnoresMonkeyPatchedIntrinsics)
{
    Babylon::AppRuntime runtime{};
    Babylon::ScriptLoader loader{runtime};

    // Replace every intrinsic the BigInt paths touch, the way user script could.
    loader.Eval(R"(
        globalThis.__pristine = { BigInt, asIntN: BigInt.asIntN, toString: BigInt.prototype.toString };
        globalThis.BigInt = function () { return globalThis.__pristine.BigInt(1234); };
        globalThis.BigInt.asIntN = function () { return globalThis.__pristine.BigInt(1234); };
        globalThis.BigInt.asUintN = function () { return globalThis.__pristine.BigInt(1234); };
        globalThis.__pristine.BigInt.prototype.toString = function () { return '1234'; };
    )",
        "");

    std::promise<void> done;
    struct { bool supported; int64_t roundTripped; bool lossless; napi_valuetype type; } observed{};

    runtime.Dispatch([&done, &observed](Napi::Env env) {
        napi_env nenv{env};

        napi_value big{nullptr};
        if (napi_create_bigint_int64(nenv, 9007199254740993LL, &big) != napi_ok)
        {
            // Engine without BigInt (jsc-android r250231, Win10 Chakra): it throws ENOTSUP instead.
            napi_value pending{nullptr};
            napi_get_and_clear_last_exception(nenv, &pending);
            observed.supported = false;
            done.set_value();
            return;
        }
        observed.supported = true;
        napi_typeof(nenv, big, &observed.type);
        napi_get_value_bigint_int64(nenv, big, &observed.roundTripped, &observed.lossless);
        done.set_value();
    });

    done.get_future().get();

    if (!observed.supported)
    {
        GTEST_SKIP() << "Engine does not support BigInt";
    }
    EXPECT_EQ(napi_bigint, observed.type);
    // 1234 here would mean a patched intrinsic was consulted.
    EXPECT_EQ(9007199254740993LL, observed.roundTripped);
    EXPECT_TRUE(observed.lossless);
}
#endif

// napi_detach_arraybuffer is the API that defines N-API v7, and its behaviour is not uniform across
// the engines here: ArrayBuffer.prototype.transfer() (ES2024) is the only public detach path -- the
// JavaScriptCore C API has no detach entry point at all -- so an engine without it can only report
// the capability as missing. This asserts both halves of that contract, whichever applies:
//
//   detach works        V8; JavaScriptCore on macOS 14.4+ / iOS 17.4+ / visionOS 1.1+
//   ENOTSUP thrown      JavaScriptCore on older Apple OSes, and every jsc-android build
//                       (verified on device: r250231 and r294992 both lack transfer)
#if !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI)
TEST(NodeApi, DetachArrayBufferOrReportsUnsupported)
{
    Babylon::AppRuntime runtime{};

    std::promise<void> done;
    struct
    {
        napi_status queryBefore{napi_ok};
        napi_status queryAfter{napi_ok};
        bool detachedBefore{true};
        bool detachedAfter{false};
        bool supported{false};
        std::string code;
    } observed;

    runtime.Dispatch([&done, &observed](Napi::Env env) {
        napi_env nenv{env};

        Napi::ArrayBuffer buffer{Napi::ArrayBuffer::New(env, 8)};
        napi_value value{buffer};

        observed.queryBefore = napi_is_detached_arraybuffer(nenv, value, &observed.detachedBefore);

        if (napi_detach_arraybuffer(nenv, value) == napi_ok)
        {
            observed.supported = true;
            observed.queryAfter = napi_is_detached_arraybuffer(nenv, value, &observed.detachedAfter);
        }
        else
        {
            // Feature-detected failure must be a catchable JS error carrying code ENOTSUP, not a
            // bare napi_status an addon cannot distinguish from a real error.
            napi_value pending{nullptr};
            napi_get_and_clear_last_exception(nenv, &pending);
            if (pending != nullptr)
            {
                napi_value code{nullptr};
                if (napi_get_named_property(nenv, pending, "code", &code) == napi_ok)
                {
                    char buffer[32]{};
                    size_t written{0};
                    napi_get_value_string_utf8(nenv, code, buffer, sizeof(buffer), &written);
                    observed.code.assign(buffer, written);
                }
            }
        }
        done.set_value();
    });

    done.get_future().get();

    ASSERT_EQ(napi_ok, observed.queryBefore) << "napi_is_detached_arraybuffer failed on a live buffer";
    EXPECT_FALSE(observed.detachedBefore) << "a live ArrayBuffer must not report as detached";
    if (observed.supported)
    {
        ASSERT_EQ(napi_ok, observed.queryAfter) << "napi_is_detached_arraybuffer failed after detach";
        EXPECT_TRUE(observed.detachedAfter) << "napi_detach_arraybuffer returned ok but did not detach";
    }
    else
    {
        EXPECT_EQ("ENOTSUP", observed.code);
    }
}
#endif

#if defined(JSRUNTIMEHOST_NAPI_ENGINE_V8)
TEST(AppRuntime, V8FinalizersDrainAfterDispatch)
{
    constexpr size_t ExternalCount{32};
    std::atomic<size_t> finalized{};
    std::promise<void> created;
    std::promise<void> collectionRequested;
    std::promise<size_t> observed;

    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&finalized, &created](Napi::Env env) {
        for (size_t index{}; index < ExternalCount; ++index)
        {
            Napi::External<std::atomic<size_t>>::New(
                env,
                &finalized,
                [](Napi::Env, std::atomic<size_t>* count) {
                    count->fetch_add(1, std::memory_order_relaxed);
                });
        }
        created.set_value();
    });
    created.get_future().wait();

    runtime.Dispatch([&collectionRequested](Napi::Env env) {
        Napi::GetContext(env)->GetIsolate()->LowMemoryNotification();
        collectionRequested.set_value();
    });
    collectionRequested.get_future().wait();

    runtime.Dispatch([&finalized, &observed](Napi::Env) {
        observed.set_value(finalized.load(std::memory_order_relaxed));
    });

    EXPECT_EQ(observed.get_future().get(), ExternalCount);
}

TEST(AppRuntime, V8FinalizerDrainYieldsBetweenDispatcherTurns)
{
    constexpr size_t ExternalCount{16};
    std::atomic<size_t> finalized{};
    std::promise<void> created;
    std::promise<void> collectionRequested;

    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&](Napi::Env env) {
        for (size_t index{}; index < ExternalCount; ++index)
        {
            Napi::External<std::atomic<size_t>>::New(
                env,
                &finalized,
                [](Napi::Env, std::atomic<size_t>* count) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{2});
                    count->fetch_add(1, std::memory_order_relaxed);
                });
        }
        created.set_value();
    });
    created.get_future().wait();

    runtime.Dispatch([&](Napi::Env env) {
        Napi::GetContext(env)->GetIsolate()->LowMemoryNotification();
        collectionRequested.set_value();
    });
    collectionRequested.get_future().wait();

    const auto observeFinalized = [&]() {
        std::promise<size_t> observed;
        auto future = observed.get_future();
        runtime.Dispatch([&](Napi::Env) {
            observed.set_value(finalized.load(std::memory_order_relaxed));
        });
        return future.get();
    };

    auto observed = observeFinalized();
    EXPECT_GT(observed, 0u);
    EXPECT_LT(observed, ExternalCount);

    for (size_t turn{}; turn < ExternalCount && observed < ExternalCount; ++turn)
    {
        observed = observeFinalized();
    }
    EXPECT_EQ(observed, ExternalCount);
}
#endif

#if defined(JSRUNTIMEHOST_TEST_WORKER) && !defined(__ANDROID__) && \
    (defined(JSR_NAPI_ENGINE_JAVASCRIPTCORE) || defined(JSR_NAPI_ENGINE_QUICKJS) || \
     defined(JSR_NAPI_ENGINE_V8) || defined(JSR_NAPI_ENGINE_HERMES))
TEST(Worker, PreservesHostDOMException)
{
    std::promise<void> done;
    Babylon::AppRuntime runtime{};

    runtime.Dispatch([&done](Napi::Env env) {
        auto global = env.Global();
        const auto hostDOMException =
            Napi::Function::New(env, [](const Napi::CallbackInfo&) {}, "HostDOMException");
        global.Set("DOMException", hostDOMException);

        Babylon::Polyfills::Worker::Options options{};
        options.ScriptRoot = std::filesystem::current_path().string();
        Babylon::Polyfills::Worker::Initialize(env, std::move(options));

        // Worker is composed with independent browser polyfills. Installing
        // its lifecycle/event glue must not invalidate exceptions created by
        // IndexedDB (or another host implementation) before Worker starts.
        EXPECT_TRUE(global.Get("DOMException").StrictEquals(hostDOMException));
        done.set_value();
    });

    done.get_future().get();
}

TEST(Worker, WebPlatformTests)
{
    struct Result
    {
        bool Passed{};
        std::string Detail{};
    };

    std::promise<Result> completion;
    std::atomic_bool completed{false};

    Babylon::AppRuntime::Options runtimeOptions{};
    runtimeOptions.UnhandledExceptionHandler = [&completion, &completed](const Napi::Error& error) {
        if (!completed.exchange(true))
        {
            completion.set_value({false, Napi::GetErrorString(error)});
        }
    };

    Babylon::AppRuntime runtime{std::move(runtimeOptions)};
    runtime.Dispatch([&completion, &completed](Napi::Env env) {
        Babylon::Polyfills::Scheduling::Initialize(env);
        Babylon::Polyfills::URL::Initialize(env);

        Babylon::Polyfills::Worker::Options options{};
        options.ScriptRoot = (std::filesystem::current_path() / "WebPlatformTests").string();
        options.ConsoleCallback = [](const char* message) {
            std::cerr << "[Worker] " << message << std::endl;
        };
        Babylon::Polyfills::Worker::Initialize(env, std::move(options));

#if defined(JSR_NAPI_ENGINE_JAVASCRIPTCORE)
        // System JSC exposes the execution-time-limit hook used to interrupt
        // a worker stuck in top-level evaluation. Other adapters currently
        // terminate cooperatively between dispatches, so the infinite-loop
        // WPT regression is intentionally JSC-only for now.
        env.Global().Set("__jsrhCanInterruptWorker", Napi::Boolean::New(env, true));
#else
        env.Global().Set("__jsrhCanInterruptWorker", Napi::Boolean::New(env, false));
#endif

        env.Global().Set("__jsrhWptDone", Napi::Function::New(
            env,
            [&completion, &completed](const Napi::CallbackInfo& info) {
                if (!completed.exchange(true))
                {
                    completion.set_value({
                        info[0].ToBoolean().Value(),
                        info[1].ToString().Utf8Value(),
                    });
                }
            },
            "__jsrhWptDone"));
        env.Global().Set("__jsrhWptProgress", Napi::Function::New(
            env,
            [](const Napi::CallbackInfo& info) {
                std::cerr << "[Worker WPT] " << info[0].ToString().Utf8Value() << std::endl;
            },
            "__jsrhWptProgress"));
    });

    Babylon::ScriptLoader loader{runtime};
    loader.LoadScript("app:///WebPlatformTests/runner.js");

    auto future = completion.get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds{90}), std::future_status::ready)
        << "Worker WPT subset timed out";
    const auto result = future.get();
    EXPECT_TRUE(result.Passed) << result.Detail;
}
#endif

// The V8JSI Node-API shim does not implement napi_create_dataview /
// napi_get_dataview_info (its DataView::New throws "TODO"), so this native test
// only builds on the Chakra, V8, and JavaScriptCore backends. The size_t-width
// guard is required because the overflow scenario below needs a 64-bit size_t.
#if (SIZE_MAX > 0xFFFFFFFFu) && !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI)
TEST(NodeApi, CreateDataViewRejectsOverflowingRange)
{
    // Regression: napi_create_dataview must reject a (byte_offset, byte_length)
    // pair whose sum overflows size_t. The pre-fix code performed an unchecked
    // `byte_offset + byte_length > bufferLength` comparison; with the inputs
    // below the 64-bit sum wraps to 8 and slips past it. It then truncated the
    // values to 32-bit (offset -> 0, length -> 8) and created a valid 8-byte
    // DataView, but stored the ORIGINAL 64-bit offset/length in DataViewInfo,
    // which napi_get_dataview_info hands back alongside the small real buffer --
    // an out-of-bounds access primitive. This path is not reachable from JS
    // `new DataView`, so it is covered natively here. The scenario requires a
    // 64-bit size_t (where the 32-bit truncation diverged from the stored value),
    // hence the size_t-width guard.
    Babylon::AppRuntime runtime{};

    std::promise<bool> overflowSafe;
    std::promise<bool> validAccepted;

    runtime.Dispatch([&overflowSafe, &validAccepted](Napi::Env env) {
        napi_env nenv{env};

        Napi::ArrayBuffer arrayBuffer{Napi::ArrayBuffer::New(env, 16)};
        napi_value arrayBufferValue{arrayBuffer};

        // Low 32 bits are individually valid for the 16-byte buffer (offset 0,
        // length 8), but the full 64-bit values are enormous and their sum wraps
        // around size_t to 8.
        const size_t hugeOffset{0xFFFFFFFF00000000ull};
        const size_t hugeLength{0x0000000100000008ull};

        napi_value result{nullptr};
        napi_status status{napi_create_dataview(nenv, hugeLength, arrayBufferValue, hugeOffset, &result)};

        bool safe;
        if (status != napi_ok || result == nullptr)
        {
            // Fixed path: the out-of-range request is rejected outright.
            safe = true;
        }
        else
        {
            // If creation unexpectedly succeeds, the reported extents must still
            // lie within the 16-byte backing buffer (i.e. not the raw 64-bit
            // inputs). The pre-fix code reported the huge stored values here.
            size_t reportedLength{0};
            size_t reportedOffset{0};
            void* data{nullptr};
            napi_get_dataview_info(nenv, result, &reportedLength, &data, nullptr, &reportedOffset);
            safe = reportedOffset <= 16 && reportedLength <= 16 && reportedOffset + reportedLength <= 16;
        }

        // Clear any pending range error so it doesn't surface as an unhandled error.
        napi_value pendingException{nullptr};
        napi_get_and_clear_last_exception(nenv, &pendingException);
        overflowSafe.set_value(safe);

        // A legitimate offset/length pair must still succeed.
        napi_value validResult{nullptr};
        napi_status validStatus{napi_create_dataview(nenv, 8, arrayBufferValue, 4, &validResult)};
        validAccepted.set_value(validStatus == napi_ok && validResult != nullptr);
    });

    EXPECT_TRUE(overflowSafe.get_future().get());
    EXPECT_TRUE(validAccepted.get_future().get());
}
#endif

// The V8JSI Node-API shim does not expose napi_get_value_string_utf16, so this
// native test only builds on the Chakra, V8, and JavaScriptCore backends.
#if !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI)
TEST(NodeApi, GetValueStringUtf16HandlesZeroBufsize)
{
    // Regression: napi_get_value_string_utf16 with a non-null buffer and
    // bufsize == 0 must not evaluate bufsize - 1. On the Chakra backend the
    // pre-fix code forwarded bufsize - 1 (== SIZE_MAX) to JsCopyStringUtf16 as
    // the destination capacity, copying the entire JS string into the
    // zero-length buffer, and then stored the terminator at buf[bufsize - 1]
    // (== buf[SIZE_MAX]). The call must instead write nothing and report zero.
    Babylon::AppRuntime runtime{};

    std::promise<bool> zeroSafe;
    std::promise<bool> normalWorks;

    runtime.Dispatch([&zeroSafe, &normalWorks](Napi::Env env) {
        napi_env nenv{env};

        napi_value strValue{Napi::String::New(env, "hello world")};

        // Sentinel-filled buffer. With bufsize == 0 nothing may be written, so
        // every element must survive unchanged (a SIZE_MAX-capacity copy would
        // clobber it / overflow).
        char16_t guard[8];
        for (auto& c : guard)
        {
            c = static_cast<char16_t>(0x7FFF);
        }

        size_t copied{0xDEAD};
        napi_status status{napi_get_value_string_utf16(nenv, strValue, guard, 0, &copied)};

        bool safe{status == napi_ok && copied == 0};
        for (auto c : guard)
        {
            safe = safe && (c == static_cast<char16_t>(0x7FFF));
        }
        zeroSafe.set_value(safe);

        // A sufficiently-sized buffer must still copy and null-terminate.
        char16_t buf[32];
        size_t copied2{0};
        napi_status status2{napi_get_value_string_utf16(nenv, strValue, buf, 32, &copied2)};
        normalWorks.set_value(status2 == napi_ok && copied2 == 11 && buf[copied2] == 0);
    });

    EXPECT_TRUE(zeroSafe.get_future().get());
    EXPECT_TRUE(normalWorks.get_future().get());
}

// Closes an escapable handle scope however the test leaves it. Without this, a
// failing assertion returns with the scope still open, the enclosing
// Napi::HandleScope then fails to close, and Napi::Error::Fatal throws out of its
// implicitly-noexcept destructor -- so the process terminates with no FAILED line
// instead of reporting the assertion.
class ScopedEscapableHandleScope
{
public:
    ScopedEscapableHandleScope(napi_env env, napi_escapable_handle_scope scope)
        : m_env{env}
        , m_scope{scope}
    {
    }

    ~ScopedEscapableHandleScope()
    {
        Close();
    }

    ScopedEscapableHandleScope(const ScopedEscapableHandleScope&) = delete;
    ScopedEscapableHandleScope& operator=(const ScopedEscapableHandleScope&) = delete;

    napi_status Close()
    {
        if (m_scope == nullptr)
        {
            return napi_ok;
        }

        const napi_escapable_handle_scope scope{m_scope};
        m_scope = nullptr;
        return napi_close_escapable_handle_scope(m_env, scope);
    }

private:
    napi_env m_env;
    napi_escapable_handle_scope m_scope;
};

// Regression: a handle returned by napi_escape_handle must stay alive after its
// escapable scope is closed. The escaped handle is stored in the parent scope, so
// closing the scope must not free it along with the scope's own handles. This is the
// contract Napi::ObjectReference::Get relies on, which in turn is what
// Napi::Error::Message and Napi::Error::what use, so getting it wrong turns any
// report of a native error message into a use-after-free.
TEST(NodeApi, EscapedHandleOutlivesItsScope)
{
    Babylon::AppRuntime runtime{};

    std::promise<bool> escapedValueIsIntact;

    runtime.Dispatch([&escapedValueIsIntact](Napi::Env env) mutable {
        napi_env nenv{env};

        // Assertions stay on the test thread: the dispatched lambda reports through the
        // promise and returns early on failure so the waiter can never deadlock.
        napi_escapable_handle_scope scope{};
        if (napi_open_escapable_handle_scope(nenv, &scope) != napi_ok)
        {
            escapedValueIsIntact.set_value(false);
            return;
        }
        ScopedEscapableHandleScope scopeGuard{nenv, scope};

        napi_value inner{};
        if (napi_create_string_utf8(nenv, "escape me", NAPI_AUTO_LENGTH, &inner) != napi_ok)
        {
            escapedValueIsIntact.set_value(false);
            return;
        }

        napi_value escaped{};
        if (napi_escape_handle(nenv, scope, inner, &escaped) != napi_ok)
        {
            escapedValueIsIntact.set_value(false);
            return;
        }

        if (scopeGuard.Close() != napi_ok)
        {
            escapedValueIsIntact.set_value(false);
            return;
        }

        // Allocate through the parent scope so a dangling escaped handle is likely to
        // have been reused by the time it is read back.
        for (int i = 0; i < 32; ++i)
        {
            napi_value filler{};
            napi_create_string_utf8(nenv, "filler filler filler", NAPI_AUTO_LENGTH, &filler);
        }

        char buffer[32]{};
        size_t copied{0};
        const napi_status status{napi_get_value_string_utf8(nenv, escaped, buffer, sizeof(buffer), &copied)};
        escapedValueIsIntact.set_value(status == napi_ok && copied == 9 && std::string{buffer} == "escape me");
    });

    EXPECT_TRUE(escapedValueIsIntact.get_future().get());
}

// Regression: two escapable scopes open at once, both escaping before either closes,
// then closed innermost first. An implementation that stores an escaped handle by
// inserting it into the middle of the handle stack shifts every entry above it,
// silently invalidating the start index the still-open inner scope was handed. Closing
// the inner scope then keeps the wrong slot and frees the inner escaped handle,
// reintroducing the dangling napi_value this fix is about.
//
// Engines differ on whether the outer scope may escape while an inner one is open, so
// the test only requires that of the engines that allow it.
TEST(NodeApi, NestedEscapableScopesBothEscape)
{
    Babylon::AppRuntime runtime{};

    std::promise<bool> bothValuesIntact;

    runtime.Dispatch([&bothValuesIntact](Napi::Env env) mutable {
        napi_env nenv{env};

        const auto fail = [&bothValuesIntact]() { bothValuesIntact.set_value(false); };

        napi_escapable_handle_scope outerScope{};
        if (napi_open_escapable_handle_scope(nenv, &outerScope) != napi_ok)
        {
            return fail();
        }
        ScopedEscapableHandleScope outerGuard{nenv, outerScope};

        // Give the outer scope handles of its own, so the inner scope starts at a
        // different index and the shifting bug is observable.
        for (int i = 0; i < 4; ++i)
        {
            napi_value outerFiller{};
            if (napi_create_string_utf8(nenv, "outer filler", NAPI_AUTO_LENGTH, &outerFiller) != napi_ok)
            {
                return fail();
            }
        }

        napi_value outerSource{};
        if (napi_create_string_utf8(nenv, "outer value", NAPI_AUTO_LENGTH, &outerSource) != napi_ok)
        {
            return fail();
        }

        napi_escapable_handle_scope innerScope{};
        if (napi_open_escapable_handle_scope(nenv, &innerScope) != napi_ok)
        {
            return fail();
        }
        ScopedEscapableHandleScope innerGuard{nenv, innerScope};

        napi_value innerSource{};
        if (napi_create_string_utf8(nenv, "inner value", NAPI_AUTO_LENGTH, &innerSource) != napi_ok)
        {
            return fail();
        }

        // Inner escapes first, then the still-open outer scope escapes.
        napi_value innerEscaped{};
        if (napi_escape_handle(nenv, innerScope, innerSource, &innerEscaped) != napi_ok)
        {
            return fail();
        }

        // Hermes only permits escaping from the innermost open scope and reports
        // napi_handle_scope_mismatch here. That is a legitimate refusal rather than a
        // failure, so record whether the engine allows this and keep checking the part
        // that applies either way.
        napi_value outerEscaped{};
        const napi_status outerEscapeStatus{napi_escape_handle(nenv, outerScope, outerSource, &outerEscaped)};
        const bool outerEscapeSupported{outerEscapeStatus == napi_ok};
        if (!outerEscapeSupported && outerEscapeStatus != napi_handle_scope_mismatch)
        {
            return fail();
        }

        // Close innermost first, as the scopes must be.
        if (innerGuard.Close() != napi_ok)
        {
            return fail();
        }

        // The inner escaped handle now belongs to the outer scope and must still read
        // back while that scope is open. Churn allocations first: a wrongly freed handle
        // only reads back wrong once its block has been reused, so allocate enough to
        // make that near certain rather than a matter of luck.
        for (int i = 0; i < 512; ++i)
        {
            napi_value filler{};
            napi_create_string_utf8(nenv, "filler filler filler", NAPI_AUTO_LENGTH, &filler);
        }

        char innerBuffer[32]{};
        size_t innerCopied{0};
        if (napi_get_value_string_utf8(nenv, innerEscaped, innerBuffer, sizeof(innerBuffer), &innerCopied) != napi_ok ||
            std::string{innerBuffer} != "inner value")
        {
            return fail();
        }

        if (outerGuard.Close() != napi_ok)
        {
            return fail();
        }

        for (int i = 0; i < 512; ++i)
        {
            napi_value filler{};
            napi_create_string_utf8(nenv, "filler filler filler", NAPI_AUTO_LENGTH, &filler);
        }

        if (!outerEscapeSupported)
        {
            // Nothing escaped from the outer scope, so the inner check above is the whole
            // result on this engine.
            bothValuesIntact.set_value(true);
            return;
        }

        char outerBuffer[32]{};
        size_t outerCopied{0};
        const napi_status status{napi_get_value_string_utf8(nenv, outerEscaped, outerBuffer, sizeof(outerBuffer), &outerCopied)};
        bothValuesIntact.set_value(status == napi_ok && std::string{outerBuffer} == "outer value");
    });

    EXPECT_TRUE(bothValuesIntact.get_future().get());
}

// Node-API permits at most one escape per escapable scope. The second call must be
// rejected with napi_escape_called_twice, and must leave the first escaped handle
// untouched rather than replacing or freeing it.
TEST(NodeApi, SecondEscapeIsRejected)
{
    Babylon::AppRuntime runtime{};

    std::promise<bool> secondEscapeRejected;
    std::promise<bool> firstValueIntact;

    runtime.Dispatch([&secondEscapeRejected, &firstValueIntact](Napi::Env env) mutable {
        napi_env nenv{env};

        const auto fail = [&secondEscapeRejected, &firstValueIntact]() {
            secondEscapeRejected.set_value(false);
            firstValueIntact.set_value(false);
        };

        napi_escapable_handle_scope scope{};
        if (napi_open_escapable_handle_scope(nenv, &scope) != napi_ok)
        {
            return fail();
        }
        ScopedEscapableHandleScope scopeGuard{nenv, scope};

        napi_value first{};
        napi_value second{};
        if (napi_create_string_utf8(nenv, "first", NAPI_AUTO_LENGTH, &first) != napi_ok ||
            napi_create_string_utf8(nenv, "second", NAPI_AUTO_LENGTH, &second) != napi_ok)
        {
            return fail();
        }

        napi_value firstEscaped{};
        if (napi_escape_handle(nenv, scope, first, &firstEscaped) != napi_ok)
        {
            return fail();
        }

        napi_value secondEscaped{};
        secondEscapeRejected.set_value(
            napi_escape_handle(nenv, scope, second, &secondEscaped) == napi_escape_called_twice);

        if (scopeGuard.Close() != napi_ok)
        {
            firstValueIntact.set_value(false);
            return;
        }

        for (int i = 0; i < 32; ++i)
        {
            napi_value filler{};
            napi_create_string_utf8(nenv, "filler filler filler", NAPI_AUTO_LENGTH, &filler);
        }

        char buffer[32]{};
        size_t copied{0};
        const napi_status status{napi_get_value_string_utf8(nenv, firstEscaped, buffer, sizeof(buffer), &copied)};
        firstValueIntact.set_value(status == napi_ok && std::string{buffer} == "first");
    });

    EXPECT_TRUE(secondEscapeRejected.get_future().get());
    EXPECT_TRUE(firstValueIntact.get_future().get());
}

// Regression: two escapable scopes opened with no handle allocated between them.
// An implementation whose opaque token is derived from a position in the handle
// stack hands both scopes the same token, so the second scope to escape is refused
// with napi_escape_called_twice despite never having escaped. Deriving the token
// from a counter instead keeps the two apart.
TEST(NodeApi, AdjacentEscapableScopesEscapeIndependently)
{
    Babylon::AppRuntime runtime{};

    std::promise<bool> bothEscapesAccepted;

    runtime.Dispatch([&bothEscapesAccepted](Napi::Env env) mutable {
        napi_env nenv{env};

        const auto fail = [&bothEscapesAccepted]() { bothEscapesAccepted.set_value(false); };

        napi_escapable_handle_scope outerScope{};
        if (napi_open_escapable_handle_scope(nenv, &outerScope) != napi_ok)
        {
            return fail();
        }
        ScopedEscapableHandleScope outerGuard{nenv, outerScope};

        // Deliberately allocate nothing here: this is what makes the two scopes share a
        // position in the handle stack.
        napi_escapable_handle_scope innerScope{};
        if (napi_open_escapable_handle_scope(nenv, &innerScope) != napi_ok)
        {
            return fail();
        }
        ScopedEscapableHandleScope innerGuard{nenv, innerScope};

        napi_value innerSource{};
        if (napi_create_string_utf8(nenv, "inner value", NAPI_AUTO_LENGTH, &innerSource) != napi_ok)
        {
            return fail();
        }

        napi_value innerEscaped{};
        if (napi_escape_handle(nenv, innerScope, innerSource, &innerEscaped) != napi_ok)
        {
            return fail();
        }

        napi_value outerSource{};
        if (napi_create_string_utf8(nenv, "outer value", NAPI_AUTO_LENGTH, &outerSource) != napi_ok)
        {
            return fail();
        }

        // The outer scope has not escaped yet, so this must not be refused.
        napi_value outerEscaped{};
        const napi_status outerEscapeStatus{napi_escape_handle(nenv, outerScope, outerSource, &outerEscaped)};
        if (outerEscapeStatus == napi_handle_scope_mismatch)
        {
            // Engines that only allow escaping from the innermost open scope cannot
            // exercise this case at all; the inner escape above is the whole result.
            bothEscapesAccepted.set_value(true);
            return;
        }
        if (outerEscapeStatus != napi_ok)
        {
            return fail();
        }

        if (innerGuard.Close() != napi_ok || outerGuard.Close() != napi_ok)
        {
            return fail();
        }

        bothEscapesAccepted.set_value(true);
    });

    EXPECT_TRUE(bothEscapesAccepted.get_future().get());
}

#endif

// The V8JSI shim surfaces a script `throw` of a primitive as a jsi::JSError rather than a
// Napi::Error, which AppRuntime's dispatch treats as fatal, so this case cannot run there.
#if !defined(JSRUNTIMEHOST_NAPI_ENGINE_JSI)
TEST(NodeApi, PrimitiveExceptionSurvivesNativeCatch)
{
    // Regression: a JavaScript `throw` of a non-object reaches node-addon-api's
    // Napi::Error, which wraps the pending exception with napi_create_reference.
    // The JavaScriptCore backend handed the primitive to a JSObject* entry point
    // (a reinterpret_cast whose assert is compiled out) and tripped a
    // RELEASE_ASSERT inside the engine. Its execution-time-limit termination
    // exception is such a string, so terminating a busy worker killed the
    // process.
    Babylon::AppRuntime runtime{};

    std::promise<bool> caught;
    std::promise<bool> runtimeStillWorks;

    runtime.Dispatch([&caught, &runtimeStillWorks](Napi::Env env) {
        bool sawError{false};
        try
        {
            // Napi::Eval rather than Env::RunScript: the JSI shim has no RunScript and
            // Hermes only implements the 3-argument napi_run_script.
            Napi::Eval(env, "throw 'plain text';", "primitive-exception.js");
        }
        catch (const Napi::Error& error)
        {
            // Must be callable whether the backend held the string itself or
            // wrapped it in an object.
            (void)error.Message();
            sawError = true;
        }
        caught.set_value(sawError);

        const auto sum = Napi::Eval(env, "1 + 1", "primitive-exception.js");
        runtimeStillWorks.set_value(sum.IsNumber() && sum.As<Napi::Number>().Int32Value() == 2);
    });

    EXPECT_TRUE(caught.get_future().get());
    EXPECT_TRUE(runtimeStillWorks.get_future().get());
}
#endif

#if defined(JSRUNTIMEHOST_NAPI_ENGINE_JAVASCRIPTCORE)
TEST(NodeApi, PropertyAccessCoercesPrimitiveReceiver)
{
    // Node coerces the receiver of the property entry points with ToObject: the
    // "length" of a string reads through its wrapper, while null and undefined
    // report napi_object_expected and leave the TypeError pending. The
    // JavaScriptCore backend used to reinterpret the primitive as an object.
    Babylon::AppRuntime runtime{};

    std::promise<bool> coerced;
    std::promise<bool> rejected;
    std::promise<bool> calledOnPrimitive;

    runtime.Dispatch([&coerced, &rejected, &calledOnPrimitive](Napi::Env env) {
        napi_env nenv{env};

        napi_value text{Napi::String::New(env, "hello")};
        napi_value length{};
        int32_t value{};
        coerced.set_value(
            napi_get_named_property(nenv, text, "length", &length) == napi_ok &&
            napi_get_value_int32(nenv, length, &value) == napi_ok &&
            value == 5);

        napi_value undefined{env.Undefined()};
        napi_value ignored{};
        const napi_status status{napi_get_named_property(nenv, undefined, "length", &ignored)};
        bool pending{false};
        napi_is_exception_pending(nenv, &pending);
        napi_value exception{};
        napi_get_and_clear_last_exception(nenv, &exception);
        rejected.set_value(status == napi_object_expected && pending);

        // A primitive receiver is boxed for napi_call_function as well.
        napi_value toUpperCase{env.Global().Get("String").As<Napi::Object>().Get("prototype").As<Napi::Object>().Get("toUpperCase")};
        napi_value upper{};
        calledOnPrimitive.set_value(
            napi_call_function(nenv, text, toUpperCase, 0, nullptr, &upper) == napi_ok &&
            Napi::Value(env, upper).As<Napi::String>().Utf8Value() == "HELLO");
    });

    EXPECT_TRUE(coerced.get_future().get());
    EXPECT_TRUE(rejected.get_future().get());
    EXPECT_TRUE(calledOnPrimitive.get_future().get());
}

TEST(NodeApi, ReferencesToPrimitivesFollowNode)
{
    // Symbols have always been referenceable, and a weak reference keeps
    // resolving while the symbol is alive. Other primitives are refused before
    // Node-API 10; from 10 on they are held while the count is positive and
    // released at zero, when the value reads back as NULL.
    Babylon::AppRuntime runtime{};

    std::promise<bool> primitivesHandled;
    std::promise<bool> symbolResolves;

    runtime.Dispatch([&primitivesHandled, &symbolResolves](Napi::Env env) {
        napi_env nenv{env};

        napi_value text{Napi::String::New(env, "held")};
        napi_ref ref{};
        const napi_status status{napi_create_reference(nenv, text, 1, &ref)};
#if NAPI_VERSION >= 10
        napi_value value{};
        uint32_t count{1};
        bool ok{status == napi_ok &&
            napi_get_reference_value(nenv, ref, &value) == napi_ok &&
            value != nullptr &&
            Napi::Value(env, value).As<Napi::String>().Utf8Value() == "held" &&
            napi_reference_unref(nenv, ref, &count) == napi_ok &&
            count == 0};
        value = text;
        ok = ok &&
            napi_get_reference_value(nenv, ref, &value) == napi_ok &&
            value == nullptr &&
            napi_reference_unref(nenv, ref, &count) == napi_generic_failure &&
            napi_delete_reference(nenv, ref) == napi_ok;
        primitivesHandled.set_value(ok);
#else
        primitivesHandled.set_value(status == napi_invalid_arg);
#endif

        napi_value symbol{Napi::Symbol::New(env, "tag")};
        napi_ref symbolRef{};
        napi_value resolved{};
        symbolResolves.set_value(
            napi_create_reference(nenv, symbol, 0, &symbolRef) == napi_ok &&
            napi_get_reference_value(nenv, symbolRef, &resolved) == napi_ok &&
            resolved != nullptr &&
            Napi::Value(env, resolved).StrictEquals(Napi::Value(env, symbol)) &&
            napi_delete_reference(nenv, symbolRef) == napi_ok);
    });

    EXPECT_TRUE(primitivesHandled.get_future().get());
    EXPECT_TRUE(symbolResolves.get_future().get());
}
#endif

TEST(Performance, TimeOriginIsPerEnvironment)
{
    // Regression: performance.now() measured from one process-wide start time that every
    // Initialize() rewrote, so bringing up a second runtime moved the first one's clock back to
    // zero. Each environment keeps its own origin now, so the first clock stays monotonic.
    const auto initialize = [](Babylon::AppRuntime& runtime) {
        std::promise<void> done;
        runtime.Dispatch([&done](Napi::Env env) {
            Babylon::Polyfills::Performance::Initialize(env);
            done.set_value();
        });
        done.get_future().get();
    };
    const auto now = [](Babylon::AppRuntime& runtime) {
        std::promise<double> value;
        runtime.Dispatch([&value](Napi::Env env) {
            auto performance = env.Global().Get("performance").As<Napi::Object>();
            value.set_value(performance.Get("now").As<Napi::Function>().Call(performance, {}).As<Napi::Number>().DoubleValue());
        });
        return value.get_future().get();
    };

    Babylon::AppRuntime first{};
    initialize(first);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    const double before = now(first);

    Babylon::AppRuntime second{};
    initialize(second);
    const double after = now(first);

    EXPECT_GE(after, before);
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
