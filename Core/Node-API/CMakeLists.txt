# Set per-platform defaults if unspecified.
if(WIN32)
    set(NAPI_JAVASCRIPT_ENGINE "Chakra" CACHE STRING "JavaScript engine for Node-API")
elseif(APPLE)
    set(NAPI_JAVASCRIPT_ENGINE "JavaScriptCore" CACHE STRING "JavaScript engine for Node-API")
elseif(ANDROID)
    set(NAPI_JAVASCRIPT_ENGINE "V8" CACHE STRING "JavaScript engine for Node-API")
elseif(UNIX)
    set(NAPI_JAVASCRIPT_ENGINE "JavaScriptCore" CACHE STRING "JavaScript engine for Node-API")
    set(JAVASCRIPTCORE_LIBRARY "/usr/lib/x86_64-linux-gnu/libjavascriptcoregtk-4.1.so" CACHE STRING "Path to the JavaScriptCore shared library")
else()
    message(FATAL_ERROR "Unable to select Node-API JavaScript engine for platform")
endif()

set(SOURCES
    "Include/Engine/${NAPI_JAVASCRIPT_ENGINE}/napi/env.h"
    "Include/Shared/napi/js_native_api.h"
    "Include/Shared/napi/js_native_api_types.h"
    "Include/Shared/napi/napi.h"
    "Include/Shared/napi/napi-inl.h")

# env.cc contains a generic `Napi::Eval` that goes through the C++ wrapper's
# `Env::RunScript`, which calls our 4-argument `napi_run_script` (Babylon
# extension carrying a `source_url`).  Hermes only provides the standard
# 3-argument `napi_run_script`, so for Hermes we skip env.cc and provide a
# Hermes-native `Napi::Eval` in env_hermes.cc that calls `hermes_run_script`.
if(NOT NAPI_JAVASCRIPT_ENGINE STREQUAL "Hermes")
    list(APPEND SOURCES "Source/env.cc")
endif()

set(INCLUDE_DIRECTORIES
    PUBLIC "Include/Shared"
    PUBLIC "Include/Engine/${NAPI_JAVASCRIPT_ENGINE}")

if(NAPI_BUILD_ABI)
    if(ANDROID)
        function(napi_install_android_package name aar_path output_directory)
            file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/package-${name}.json" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
            file(RENAME "${CMAKE_CURRENT_BINARY_DIR}/package-${name}.json" "${CMAKE_CURRENT_BINARY_DIR}/package.json")
            npm(install --no-package-lock --silent WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

            file(GLOB_RECURSE ANDROID_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/node_modules/${V8_PACKAGE_NAME}/${aar_path}/*.aar")
            file(ARCHIVE_EXTRACT INPUT ${ANDROID_ARCHIVE} DESTINATION ${output_directory} PATTERNS jni)
            message(STATUS "Extracting ${V8_PACKAGE_NAME} archive - done")

            file(COPY "${CMAKE_CURRENT_BINARY_DIR}/node_modules/${V8_PACKAGE_NAME}/dist/include" DESTINATION ${output_directory})
        endfunction()
    endif()

    if(NAPI_JAVASCRIPT_ENGINE STREQUAL "QuickJS")
        set(SOURCES ${SOURCES}
            "Source/env_quickjs.cc"
            "Source/js_native_api_quickjs.cc"
            "Source/js_native_api_quickjs.h")
        set(LINK_LIBRARIES ${LINK_LIBRARIES} PUBLIC qjs)
    elseif(NAPI_JAVASCRIPT_ENGINE STREQUAL "Chakra")
        set(SOURCES ${SOURCES}
            "Source/env_chakra.cc"
            "Source/js_native_api_chakra.cc"
            "Source/js_native_api_chakra.h")

        set(LINK_LIBRARIES ${LINK_LIBRARIES}
            INTERFACE "chakrart.lib")
    elseif(NAPI_JAVASCRIPT_ENGINE STREQUAL "JavaScriptCore")
        set(SOURCES ${SOURCES}
            "Source/env_javascriptcore.cc"
            "Source/js_native_api_javascriptcore.cc"
            "Source/js_native_api_javascriptcore.h")

        if(ANDROID)
            # Bun publishes the JavaScriptCore build used by each release as pinned Android static
            # archives. Relink the selected archives into the libjsc.so boundary expected by napi,
            # instead of installing an npm package only to unpack its embedded AAR.
            set(JSR_JSC_BUN_WEBKIT_REVISION "5488984d20e0dbfe4be2c3ba8fb18eb81a5e0e8b")
            set(JSR_JSC_BUN_WEBKIT_RELEASE "autobuild-${JSR_JSC_BUN_WEBKIT_REVISION}")
            set(JSR_JSC_ANDROID_ROOT "" CACHE PATH
                "Path to an extracted Bun Android WebKit 'bun-webkit' directory; empty downloads the pinned release")

            string(REGEX REPLACE "^android-" "" JSR_JSC_ANDROID_API_LEVEL "${ANDROID_PLATFORM}")
            if(JSR_JSC_ANDROID_API_LEVEL LESS 28)
                message(FATAL_ERROR
                    "Bun's pinned Android WebKit archives target API 28; set Android minSdkVersion to 28 or newer")
            endif()

            if(ANDROID_ABI STREQUAL "arm64-v8a")
                set(JSR_JSC_BUN_WEBKIT_ASSET "bun-webkit-linux-arm64-android.tar.gz")
                set(JSR_JSC_BUN_WEBKIT_SHA256 "63c18a18c89dc3e1c0a93107f95290c27fbe310eb90e30dc197a7936804d034b")
            elseif(ANDROID_ABI STREQUAL "x86_64")
                set(JSR_JSC_BUN_WEBKIT_ASSET "bun-webkit-linux-amd64-android.tar.gz")
                set(JSR_JSC_BUN_WEBKIT_SHA256 "c198efb68c17ae65ff66968f7d04bdab4845a5abaefb941b15e0e2b36aaab774")
            else()
                message(FATAL_ERROR
                    "Bun Android JavaScriptCore supports only arm64-v8a and x86_64; got '${ANDROID_ABI}'")
            endif()

            if(JSR_JSC_ANDROID_ROOT)
                set(JSC_ANDROID_DIR "${JSR_JSC_ANDROID_ROOT}")
            else()
                FetchContent_Declare(bun_webkit_android
                    URL
                        "https://github.com/oven-sh/WebKit/releases/download/${JSR_JSC_BUN_WEBKIT_RELEASE}/${JSR_JSC_BUN_WEBKIT_ASSET}"
                    URL_HASH "SHA256=${JSR_JSC_BUN_WEBKIT_SHA256}")
                FetchContent_GetProperties(bun_webkit_android)
                if(NOT bun_webkit_android_POPULATED)
                    message(STATUS
                        "Downloading Bun WebKit ${JSR_JSC_BUN_WEBKIT_REVISION} for ${ANDROID_ABI}")
                    FetchContent_Populate(bun_webkit_android)
                endif()
                # FetchContent strips the archive's single top-level bun-webkit directory.
                set(JSC_ANDROID_DIR "${bun_webkit_android_SOURCE_DIR}")
            endif()

            set(JSC_ANDROID_STATIC_LIBRARIES
                "${JSC_ANDROID_DIR}/lib/libJavaScriptCore.a"
                "${JSC_ANDROID_DIR}/lib/libWTF.a"
                "${JSC_ANDROID_DIR}/lib/libbmalloc.a"
                "${JSC_ANDROID_DIR}/lib/libicui18n.a"
                "${JSC_ANDROID_DIR}/lib/libicuuc.a"
                "${JSC_ANDROID_DIR}/lib/libicudata.a")

            foreach(JSC_ANDROID_FILE
                    "${JSC_ANDROID_DIR}/include/JavaScriptCore/JavaScript.h"
                    ${JSC_ANDROID_STATIC_LIBRARIES})
                if(NOT EXISTS "${JSC_ANDROID_FILE}")
                    message(FATAL_ERROR "Bun Android WebKit archive is missing '${JSC_ANDROID_FILE}'")
                endif()
            endforeach()

            set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
                PUBLIC "${JSC_ANDROID_DIR}/include")

            add_library(jsc_android SHARED "Source/jsc_android.cpp")
            set_target_properties(jsc_android PROPERTIES
                OUTPUT_NAME jsc
                CXX_STANDARD 23
                CXX_STANDARD_REQUIRED ON
                LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/jsc_android.exports")
            target_compile_definitions(jsc_android PRIVATE
                JSR_JSC_BUN_WEBKIT_REVISION="${JSR_JSC_BUN_WEBKIT_REVISION}"
                BUILDING_JSCONLY__
                BUILDING_JavaScriptCore
                BUILDING_WEBKIT=1
                BUILDING_WITH_CMAKE=1
                HAVE_CONFIG_H=1
                PAS_BMALLOC=1
                STATICALLY_LINKED_WITH_WTF
                STATICALLY_LINKED_WITH_bmalloc
                U_STATIC_IMPLEMENTATION=1)
            target_include_directories(jsc_android PUBLIC "${JSC_ANDROID_DIR}/include")
            target_link_options(jsc_android PRIVATE
                "-Wl,--no-undefined"
                "-Wl,-Bsymbolic"
                "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/Source/jsc_android.exports"
                "-Wl,-z,max-page-size=16384"
                "-Wl,-z,common-page-size=16384")
            target_link_libraries(jsc_android PRIVATE
                "-Wl,--whole-archive"
                "${JSC_ANDROID_DIR}/lib/libJavaScriptCore.a"
                "-Wl,--no-whole-archive"
                "-Wl,--start-group"
                "${JSC_ANDROID_DIR}/lib/libWTF.a"
                "${JSC_ANDROID_DIR}/lib/libbmalloc.a"
                "${JSC_ANDROID_DIR}/lib/libicui18n.a"
                "${JSC_ANDROID_DIR}/lib/libicuuc.a"
                "${JSC_ANDROID_DIR}/lib/libicudata.a"
                "-Wl,--end-group"
                log
                dl
                m)
            set_property(TARGET jsc_android PROPERTY FOLDER Dependencies)

            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC jsc_android)
        elseif(APPLE)
            find_library(JAVASCRIPTCORE_LIBRARY JavaScriptCore)
            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC ${JAVASCRIPTCORE_LIBRARY})
        elseif(UNIX)
            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC ${JAVASCRIPTCORE_LIBRARY})
            set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
                PUBLIC "/usr/include/webkitgtk-4.1/")
        else()
            message(FATAL_ERROR "Unsupported JavaScript engine: ${NAPI_JAVASCRIPT_ENGINE}")
        endif()
    elseif(NAPI_JAVASCRIPT_ENGINE STREQUAL "V8")
        set(SOURCES ${SOURCES}
            "Source/env_v8.cc"
            "Source/js_native_api_v8.cc"
            "Source/js_native_api_v8.h"
            "Source/js_native_api_v8_internals.h")

        if(ANDROID)
            set(V8_PACKAGE_BASE_NAME "v8-android-jit-nointl-nosnapshot")
            set(V8_PACKAGE_NAME "@babylonjs/${V8_PACKAGE_BASE_NAME}")
            set(V8_ANDROID_DIR "${CMAKE_CURRENT_BINARY_DIR}/${V8_PACKAGE_BASE_NAME}")
            napi_install_android_package(v8 "dist/org/chromium" ${V8_ANDROID_DIR})

            set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
                PUBLIC "${V8_ANDROID_DIR}/include")

            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC "${V8_ANDROID_DIR}/jni/${ANDROID_ABI}/libv8android.so")
        elseif(WIN32)
            set_cpu_platform_arch()
            set(V8_VERSION "11.9.169.4")
            download_nuget()
            set(V8_PACKAGE_PATH "${NUGET_PATH}/packages/v8-v143-${CPU_ARCH}.${V8_VERSION}")
            set(V8_PACKAGE_PATH "${NUGET_PATH}/packages/v8-v143-${CPU_ARCH}.${V8_VERSION}")
            set(V8_REDIST_PACKAGE_PATH "${NUGET_PATH}/packages/v8.redist-v143-${CPU_ARCH}.${V8_VERSION}")

            add_library(v8_libbase SHARED IMPORTED)
            set_target_properties(v8_libbase PROPERTIES IMPORTED_IMPLIB "${V8_PACKAGE_PATH}/lib/Release/v8_libbase.dll.lib")
            add_library(v8_libplatform SHARED IMPORTED)
            set_target_properties(v8_libplatform PROPERTIES IMPORTED_IMPLIB "${V8_PACKAGE_PATH}/lib/Release/v8_libplatform.dll.lib")
            add_library(v8 SHARED IMPORTED)
            set_target_properties(v8 PROPERTIES IMPORTED_IMPLIB "${V8_PACKAGE_PATH}/lib/Release/v8.dll.lib")
            target_link_libraries(v8 INTERFACE v8_libbase INTERFACE v8_libplatform)
            target_include_directories(v8 INTERFACE "${V8_PACKAGE_PATH}/include")

            set(V8_DIST
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/icudtl.dat"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/third_party_icu_icui18n.dll"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/third_party_abseil-cpp_absl.dll"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/icuuc.dll"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/v8.dll"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/v8_libbase.dll"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/v8_libplatform.dll"
                "${V8_REDIST_PACKAGE_PATH}/lib/Release/third_party_zlib.dll")

            # only 1 imported location per library -> Adding 1 library per file
            foreach(V8FILE ${V8_DIST})
                get_filename_component(V8FILE_NAME "${V8FILE}" NAME_WE)
                add_library("v8::${V8FILE_NAME}" SHARED IMPORTED)
                set_target_properties("v8::${V8FILE_NAME}" PROPERTIES IMPORTED_IMPLIB "${V8_PACKAGE_PATH}/lib/Release/v8_libbase.dll.lib")
                set_target_properties("v8::${V8FILE_NAME}" PROPERTIES IMPORTED_LOCATION ${V8FILE})
            endforeach()

            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC v8
                PRIVATE v8::icudtl
                PRIVATE v8::third_party_icu_icui18n
                PRIVATE v8::icuuc
                PRIVATE v8::v8
                PRIVATE v8::v8_libbase
                PRIVATE v8::v8_libplatform
                PRIVATE v8::third_party_zlib)
        else()
            message(FATAL_ERROR "Unsupported JavaScript engine: ${NAPI_JAVASCRIPT_ENGINE}")
        endif()
    elseif(NAPI_JAVASCRIPT_ENGINE STREQUAL "Hermes")
        # Hermes provides its own implementation of the standard Node-API C
        # functions inside the `hermesNapi` static library, so we don't ship a
        # `js_native_api_hermes.cc` here.  We only contribute the Babylon-side
        # bridge (`env_hermes.cc`) which adapts our `Napi::Attach`/`Detach`/
        # `Eval` helpers to Hermes's `hermes_napi_create_env` /
        # `hermes_run_script` APIs.
        set(SOURCES ${SOURCES}
            "Source/env_hermes.cc")

        if(NOT TARGET hermesNapi)
            message(FATAL_ERROR "NAPI_JAVASCRIPT_ENGINE=Hermes requires the hermesNapi target. \
Make sure Hermes was fetched at the top-level CMakeLists.txt and NAPI_JAVASCRIPT_ENGINE was set BEFORE configure.")
        endif()

        # Link Hermes PRIVATEly so its include directories and compile
        # definitions (notably `NAPI_VERSION=10` and the vendored
        # `hermes/napi/*` headers) don't leak to consumers of `napi`.
        # Consumers continue to see only our shared `napi/*` headers.
        #
        # We link against Hermes's umbrella static library `hermesvm_a` (which
        # transitively pulls in all the BCGen/Optimizer/Regex/Support/etc.
        # object libraries that `hermesVMRuntime` depends on) plus the NAPI
        # static library on top.  Linking just `hermesNapi + hermesVMRuntime +
        # hermesPublic` leaves a long tail of unresolved symbols
        # (bigint::exponentiate, regex::parseRegex, hbc::compileEvalModule,
        # llvh::raw_ostream, platform_unicode::normalize, ...) which
        # `hermesvm_a` bundles together for us.
        set(LINK_LIBRARIES ${LINK_LIBRARIES}
            PRIVATE hermesNapi
            PRIVATE hermesvm_a)

        # `hermes_napi.h` lives in Hermes's `API/napi/` source directory and
        # is NOT installed alongside the other `include/hermes/napi/`
        # headers, so the hermesNapi PUBLIC include dir doesn't expose it.
        # Add the API/napi source directory explicitly so env_hermes.cc can
        # `#include "hermes_napi.h"`.
        #
        # Additionally, Hermes uses *global* include_directories() at its
        # project root (for the vendored LLVH headers and Hermes config
        # output) instead of attaching them to each target.  Those globals
        # don't propagate out of Hermes's add_subdirectory scope, so any
        # consumer (including our napi target) that pulls in Hermes headers
        # transitively needs them re-declared here.
        #
        # We resolve the binary directory via CMAKE_BINARY_DIR rather than
        # ${hermes_BINARY_DIR} because FetchContent's lowercased per-content
        # binary variable isn't always populated reliably across CMake
        # versions/scopes, whereas the standard `_deps/<name>-build` layout
        # is.
        set(_HERMES_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/hermes-build")
        set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
            PRIVATE "${hermes_SOURCE_DIR}/API/napi"
            PRIVATE "${hermes_SOURCE_DIR}/include"
            PRIVATE "${hermes_SOURCE_DIR}/public"
            PRIVATE "${hermes_SOURCE_DIR}/external/llvh/include"
            PRIVATE "${hermes_SOURCE_DIR}/external/llvh/gen/include"
            PRIVATE "${_HERMES_BINARY_DIR}/include"
            PRIVATE "${_HERMES_BINARY_DIR}/lib/config"
            PRIVATE "${_HERMES_BINARY_DIR}/external/llvh/include")

        # Hermes's headers rely on a handful of compiler-specific warning
        # suppressions that Hermes applies globally in its own
        # `Hermes.cmake` (e.g. `-wd4576` for C-style compound literals like
        # `(SHLegacyValue){...}` inside `sh_legacy_value.h`).  Those
        # globals don't propagate to our `napi` target, so we apply the
        # minimum set needed for the headers we transitively pull in via
        # `env_hermes.cc`.  These flags are MSVC-only — clang and gcc
        # accept compound literals as a GNU extension under `-std=gnu++20`
        # (which CMake picks by default) and would otherwise interpret a
        # `/wdNNNN` argument as a missing source file path.
        if(MSVC)
            set(NAPI_HERMES_COMPILE_OPTIONS
                /wd4068  # unknown pragma (GCC pragmas in Hermes headers)
                /wd4141  # 'inline': used more than once
                /wd4146  # unary minus on unsigned type
                /wd4244  # conversion possible loss of data
                /wd4267  # size_t -> smaller type conversion
                /wd4291  # no matching delete found
                /wd4576) # parenthesized type followed by initializer list
        else()
            set(NAPI_HERMES_COMPILE_OPTIONS "")
        endif()

        # Maximum GC heap size, in MiB, for the Hermes Runtime created by
        # `Napi::Attach` (see Source/env_hermes.cc).  This is the GC's upper
        # bound -- a ceiling, not an up-front reservation.  The 512 MiB default
        # gives the unit-test Mocha suite generous headroom over Hermes's tiny
        # built-in default; embedders running heavier workloads (large Babylon
        # scenes, big typed-array buffers) can raise it at configure time with:
        #     -DNAPI_HERMES_MAX_HEAP_SIZE_MB=<value>
        # The value is forwarded to env_hermes.cc as a compile definition of the
        # same name.
        set(NAPI_HERMES_MAX_HEAP_SIZE_MB 512 CACHE STRING
            "Maximum Hermes GC heap size in MiB for the Napi::Attach runtime (default 512)")
    else()
        message(FATAL_ERROR "Unsupported JavaScript engine: ${NAPI_JAVASCRIPT_ENGINE}")
    endif()

    message(STATUS "Selected ${NAPI_JAVASCRIPT_ENGINE}")
endif()

add_library(napi ${SOURCES})

target_include_directories(napi ${INCLUDE_DIRECTORIES})
target_link_libraries(napi ${LINK_LIBRARIES})

if(NAPI_JAVASCRIPT_ENGINE STREQUAL "Hermes")
    # Apply Hermes-specific warning suppressions ONLY to env_hermes.cc so
    # they don't relax the rules for the rest of the napi sources.
    set_source_files_properties("Source/env_hermes.cc"
        PROPERTIES COMPILE_OPTIONS "${NAPI_HERMES_COMPILE_OPTIONS}")

    # Forward the configurable Hermes GC max heap size to env_hermes.cc.
    target_compile_definitions(napi PRIVATE
        NAPI_HERMES_MAX_HEAP_SIZE_MB=${NAPI_HERMES_MAX_HEAP_SIZE_MB})
endif()

set_property(TARGET napi PROPERTY FOLDER Dependencies)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
