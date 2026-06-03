cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# --------------------------------------------------
# Declarations
# --------------------------------------------------
FetchContent_Declare(AndroidExtensions
    GIT_REPOSITORY https://github.com/BabylonJS/AndroidExtensions.git
    GIT_TAG 5fe60c8634dd9a5df76888e73e69202875e894f0
    EXCLUDE_FROM_ALL)
FetchContent_Declare(arcana.cpp
    GIT_REPOSITORY https://github.com/microsoft/arcana.cpp.git
    GIT_TAG 0b9f9b6d761909fbca9d3ab1f2d8ff1c3d25ed3d
    EXCLUDE_FROM_ALL)
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG f693a3eb7fe72a5f19b975289afc4f437d373d9c
    EXCLUDE_FROM_ALL)
FetchContent_Declare(CMakeExtensions
    GIT_REPOSITORY https://github.com/BabylonJS/CMakeExtensions.git
    GIT_TAG dc750e7f69dad76779419df6442f834c57a30a1f
    EXCLUDE_FROM_ALL)
FetchContent_Declare(googletest
    URL "https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz"
    EXCLUDE_FROM_ALL)
FetchContent_Declare(hermes
    GIT_REPOSITORY https://github.com/facebook/hermes.git
    # Pinned to the tip of the `static_h` branch as of 2026-06-03 so CI is
    # reproducible.  Bump this SHA when you intentionally want to pick up
    # upstream Hermes changes — keep it on the static_h branch (Static
    # Hermes is the variant our NAPI integration targets).
    GIT_TAG 348582831f50954895da8e80cc91112d51036c69
    # Apply Babylon-side patches against the fetched Hermes source.  Each
    # patch carries the rationale in its header; see the patches dir.
    PATCH_COMMAND ${CMAKE_COMMAND}
        -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/hermes-shutdown-pre-mark-deletion-pending.patch
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatchIfNeeded.cmake
    EXCLUDE_FROM_ALL)
FetchContent_Declare(ios-cmake
    GIT_REPOSITORY https://github.com/leetal/ios-cmake.git
    GIT_TAG 4.4.1
    EXCLUDE_FROM_ALL)
FetchContent_Declare(llhttp
    URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v8.1.0.tar.gz"
    EXCLUDE_FROM_ALL)
FetchContent_Declare(UrlLib
    GIT_REPOSITORY https://github.com/BabylonJS/UrlLib.git
    GIT_TAG 16c7dfd88cb5e7121cb5b037b44b796b737955d4
    EXCLUDE_FROM_ALL)
# --------------------------------------------------

FetchContent_MakeAvailable(CMakeExtensions)

if(IOS)
    FetchContent_MakeAvailable_With_Message(ios-cmake)
    set(CMAKE_TOOLCHAIN_FILE "${ios-cmake_SOURCE_DIR}/ios.toolchain.cmake" CACHE PATH "")
    set(PLATFORM "OS64COMBINED" CACHE STRING "")
    set(DEPLOYMENT_TARGET "12" CACHE STRING "")
    set(CMAKE_XCODE_GENERATE_SCHEME TRUE)
endif()

project(JsRuntimeHost)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------
# Options
# --------------------------------------------------

# General
option(JSRUNTIMEHOST_TESTS "Include JsRuntimeHost Tests." ${PROJECT_IS_TOP_LEVEL})
option(NAPI_BUILD_ABI "Build the ABI layer." ON)
option(BABYLON_DEBUG_TRACE "Debug Trace callback."  OFF)

# Core
option(JSRUNTIMEHOST_CORE_APPRUNTIME "Include JsRuntimeHost Core AppRuntime" ON)
option(JSRUNTIMEHOST_CORE_APPRUNTIME_V8_INSPECTOR "Include the V8 inspector protocol required to debug JavaScript with a V8 debugger." ON)
option(JSRUNTIMEHOST_CORE_SCRIPTLOADER "Include JsRuntimeHost Core ScriptLoader" ON)

# Polyfills
option(JSRUNTIMEHOST_POLYFILL_CONSOLE "Include JsRuntimeHost Polyfill Console." ON)
option(JSRUNTIMEHOST_POLYFILL_SCHEDULING "Include JsRuntimeHost Polyfill Scheduling." ON)
option(JSRUNTIMEHOST_POLYFILL_XMLHTTPREQUEST "Include JsRuntimeHost Polyfill XMLHttpRequest." ON)
option(JSRUNTIMEHOST_POLYFILL_URL "Include JsRuntimeHost Polyfill URL and URLSearchParams." ON)
option(JSRUNTIMEHOST_POLYFILL_ABORT_CONTROLLER "Include JsRuntimeHost Polyfills AbortController and AbortSignal." ON)
option(JSRUNTIMEHOST_POLYFILL_WEBSOCKET "Include JsRuntimeHost Polyfill WebSocket." ON)
option(JSRUNTIMEHOST_POLYFILL_BLOB "Include JsRuntimeHost Polyfill Blob." ON)
option(JSRUNTIMEHOST_POLYFILL_PERFORMANCE "Include JsRuntimeHost Polyfill Performance." ON)
option(JSRUNTIMEHOST_POLYFILL_TEXTDECODER "Include JsRuntimeHost Polyfill TextDecoder." ON)
option(JSRUNTIMEHOST_POLYFILL_TEXTENCODER "Include JsRuntimeHost Polyfill TextEncoder." ON)

# Sanitizers
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(ENABLE_SANITIZERS AND ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_THREAD_SANITIZER cannot be used together.")
endif()

if(ENABLE_SANITIZERS)
    set(ENABLE_RTTI ON CACHE BOOL "" FORCE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(SANITIZERS "address,undefined")
        # Check for Clang since vptr and fdsan are Clang-specific
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # vptr is not supported on Windows.
            if(NOT WIN32)
                list(APPEND SANITIZERS "vptr")
            endif()
            # FDSan only works on Android builds with Clang
            if (ANDROID)
                list(APPEND SANITIZERS "fdsan")
            endif()
        endif()

        string(JOIN "," SANITIZER_FLAGS ${SANITIZERS})

        add_compile_options(-fsanitize=${SANITIZER_FLAGS} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${SANITIZER_FLAGS})
    else()
        message(WARNING "Sanitizers not supported on this compiler.")
    endif()
endif()

if(ENABLE_THREAD_SANITIZER)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    else()
        message(WARNING "ThreadSanitizer not supported on this compiler.")
    endif()
endif()

# --------------------------------------------------

if(JSRUNTIMEHOST_TESTS)
    add_compile_definitions(ARCANA_TEST_HOOKS)
endif()

FetchContent_MakeAvailable_With_Message(arcana.cpp)
set_property(TARGET arcana PROPERTY FOLDER Dependencies)

if(JSRUNTIMEHOST_POLYFILL_XMLHTTPREQUEST)
    FetchContent_MakeAvailable_With_Message(UrlLib)
    set_property(TARGET UrlLib PROPERTY FOLDER Dependencies)
endif()

if(BABYLON_DEBUG_TRACE)
    add_definitions(-DBABYLON_DEBUG_TRACE)
endif()

if(NAPI_JAVASCRIPT_ENGINE STREQUAL "Hermes")
    # Configure Hermes static_h options BEFORE making it available so that
    # cache variables take effect.  We disable the parts of Hermes we don't
    # need (the unit-test suite, debugger) to keep build time reasonable and
    # to avoid pulling extra targets into our build tree.  Notably we leave
    # HERMES_ENABLE_NAPI ON (the default) — that's the whole point of
    # integrating Hermes here.
    #
    # HERMES_ENABLE_TOOLS must stay ON: even when HERMES_ENABLE_TEST_SUITE is
    # OFF, Hermes unconditionally adds external/node-api-cts and
    # external/node-api-tests whenever HERMES_ENABLE_NAPI is on, and those
    # subdirectories reference the `hermes` CLI tool target via
    # $<TARGET_FILE:hermes>.  CMake fails to generate if the target doesn't
    # exist, so we let Hermes build its tools.  None of them ship in our
    # final binaries because they're EXCLUDE_FROM_ALL via FetchContent.
    set(HERMES_ENABLE_TOOLS ON CACHE BOOL "" FORCE)
    set(HERMES_ENABLE_TEST_SUITE OFF CACHE BOOL "" FORCE)
    set(HERMES_ENABLE_DEBUGGER OFF CACHE BOOL "" FORCE)
    set(HERMES_BUILD_APPLE_FRAMEWORK OFF CACHE BOOL "" FORCE)
    set(HERMES_ENABLE_NAPI ON CACHE BOOL "" FORCE)
    # Hermes defaults HERMES_SLOW_DEBUG=ON, which compiles in internal
    # sanity checks that fire even in optimized (RelWithDebInfo / Release)
    # builds — e.g. heap-state assertions in HadesGC and the finalizer
    # chain.  These can SIGABRT during Runtime teardown on some platforms
    # (observed on macOS arm64 RelWithDebInfo: process exited with code
    # 134 right after `145 passing`, before any gtest [OK] line, with no
    # diagnostic in stderr).  Embedders don't need them.
    set(HERMES_SLOW_DEBUG OFF CACHE BOOL "" FORCE)
    # Hermes ships its own bundled gtest as `llvh-gtest` (a different
    # CMake target name from googletest's `gtest`), so the two coexist
    # without clashing.  We never link llvh-gtest into our UnitTests
    # executable, so there's no duplicate-symbol issue at link time.
    FetchContent_MakeAvailable_With_Message(hermes)
endif()

if(NAPI_JAVASCRIPT_ENGINE STREQUAL "V8" AND JSRUNTIMEHOST_CORE_APPRUNTIME_V8_INSPECTOR)
    FetchContent_MakeAvailable_With_Message(asio)
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
    set_property(TARGET asio PROPERTY FOLDER Dependencies)

    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
    FetchContent_MakeAvailable_With_Message(llhttp)
    set_property(TARGET llhttp_static PROPERTY FOLDER Dependencies)
endif()

if(JSRUNTIMEHOST_TESTS)
    if(WIN32)
        # For Windows: Prevent overriding the parent project's compiler/linker settings
        # Default build type for my test projects are /MDd (MultiThreaded DLL) but GTests default to /MTd (MultiThreaded)
        # see https://github.com/google/googletest/blob/main/googletest/README.md
        # "Enabling this option will make gtest link the runtimes dynamically too, and match the project in which it is included."
        set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable_With_Message(googletest)

    set_property(TARGET gmock PROPERTY FOLDER Dependencies/GoogleTest)
    set_property(TARGET gmock_main PROPERTY FOLDER Dependencies/GoogleTest)
    set_property(TARGET gtest PROPERTY FOLDER Dependencies/GoogleTest)
    set_property(TARGET gtest_main PROPERTY FOLDER Dependencies/GoogleTest)
endif()

if(ANDROID)
    set(JSRUNTIMEHOST_PLATFORM "Android")
elseif(IOS)
    set(JSRUNTIMEHOST_PLATFORM "iOS")
elseif(APPLE)
    set(JSRUNTIMEHOST_PLATFORM "macOS")
elseif(WINDOWS_STORE)
    set(JSRUNTIMEHOST_PLATFORM "UWP")
elseif(WIN32)
    set(JSRUNTIMEHOST_PLATFORM "Win32")
elseif(UNIX)
    set(JSRUNTIMEHOST_PLATFORM "Unix")
else()
    message(FATAL_ERROR "Unrecognized platform: ${CMAKE_SYSTEM_NAME}")
endif()

add_subdirectory(Core)
add_subdirectory(Polyfills)

if(JSRUNTIMEHOST_TESTS AND NOT WINDOWS_STORE)
    add_subdirectory(Tests)
endif()
