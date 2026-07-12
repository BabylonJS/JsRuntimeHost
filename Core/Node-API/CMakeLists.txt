# Set per-platform defaults if unspecified.
if(WIN32)
    set(NAPI_JAVASCRIPT_ENGINE "Chakra" CACHE STRING "JavaScript engine for Node-API")
elseif(APPLE)
    set(NAPI_JAVASCRIPT_ENGINE "JavaScriptCore" CACHE STRING "JavaScript engine for Node-API")
elseif(ANDROID)
    set(NAPI_JAVASCRIPT_ENGINE "V8" CACHE STRING "JavaScript engine for Node-API")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NAPI_JAVASCRIPT_ENGINE "JavaScriptCore" CACHE STRING "JavaScript engine for Node-API")
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

        if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # Bun publishes the JavaScriptCore build used by each release as pinned static
            # archives. Download only headers and link inputs, then use Android's libjsc.so
            # boundary or Linux's final LTO executable link instead of maintaining a separate
            # JSC build/package pipeline.
            set(JSR_BUN_JSC_WEBKIT_REVISION "5488984d20e0dbfe4be2c3ba8fb18eb81a5e0e8b")
            set(JSR_BUN_JSC_WEBKIT_RELEASE "autobuild-${JSR_BUN_JSC_WEBKIT_REVISION}")
            set(JSR_BUN_JSC_ROOT "" CACHE PATH
                "Path to an extracted Bun WebKit 'bun-webkit' directory; empty downloads the pinned release")

            if(ANDROID)
                set(JSR_BUN_JSC_ANDROID_NDK_VERSION "${CMAKE_ANDROID_NDK_VERSION}")
                if(NOT JSR_BUN_JSC_ANDROID_NDK_VERSION)
                    set(JSR_BUN_JSC_ANDROID_NDK_ROOT "${CMAKE_ANDROID_NDK}")
                    if(NOT JSR_BUN_JSC_ANDROID_NDK_ROOT)
                        set(JSR_BUN_JSC_ANDROID_NDK_ROOT "${ANDROID_NDK}")
                    endif()
                    if(EXISTS "${JSR_BUN_JSC_ANDROID_NDK_ROOT}/source.properties")
                        file(STRINGS "${JSR_BUN_JSC_ANDROID_NDK_ROOT}/source.properties"
                            JSR_BUN_JSC_ANDROID_NDK_REVISION_LINE
                            REGEX "^Pkg\\.Revision = ")
                        string(REGEX REPLACE "^Pkg\\.Revision = ([0-9]+\\.[0-9]+).*$" "\\1"
                            JSR_BUN_JSC_ANDROID_NDK_VERSION
                            "${JSR_BUN_JSC_ANDROID_NDK_REVISION_LINE}")
                    endif()
                endif()
                if(NOT JSR_BUN_JSC_ANDROID_NDK_VERSION OR
                   JSR_BUN_JSC_ANDROID_NDK_VERSION VERSION_LESS "26.1")
                    message(FATAL_ERROR
                        "Bun's current WebKit headers require Android NDK r26b or newer for C++23 library support; found r${JSR_BUN_JSC_ANDROID_NDK_VERSION}")
                endif()

                string(REGEX REPLACE "^android-" "" JSR_BUN_JSC_ANDROID_API_LEVEL "${ANDROID_PLATFORM}")
                if(JSR_BUN_JSC_ANDROID_API_LEVEL LESS 28)
                    message(FATAL_ERROR
                        "Bun's pinned Android WebKit archives target API 28; set Android minSdkVersion to 28 or newer")
                endif()

                # Bun does not publish Android LTO archives. These are its optimized native
                # arm64 and x86_64 builds; the full-LTO variants are Linux-only.
                if(ANDROID_ABI STREQUAL "arm64-v8a")
                    set(JSR_BUN_JSC_ASSET "bun-webkit-linux-arm64-android.tar.gz")
                    set(JSR_BUN_JSC_SHA256 "63c18a18c89dc3e1c0a93107f95290c27fbe310eb90e30dc197a7936804d034b")
                elseif(ANDROID_ABI STREQUAL "x86_64")
                    set(JSR_BUN_JSC_ASSET "bun-webkit-linux-amd64-android.tar.gz")
                    set(JSR_BUN_JSC_SHA256 "c198efb68c17ae65ff66968f7d04bdab4845a5abaefb941b15e0e2b36aaab774")
                else()
                    message(FATAL_ERROR
                        "Bun Android JavaScriptCore supports only arm64-v8a and x86_64; got '${ANDROID_ABI}'")
                endif()
                set(JSR_BUN_JSC_OUTPUT_NAME jsc)
            else()
                include(CheckCXXSourceCompiles)
                check_cxx_source_compiles([=[
                    #include <features.h>
                    #ifndef __GLIBC__
                    #error Bun's selected Linux JavaScriptCore archive requires glibc
                    #endif
                    int main() { return 0; }
                ]=] JSR_BUN_JSC_USES_GLIBC)
                if(NOT JSR_BUN_JSC_USES_GLIBC)
                    message(FATAL_ERROR
                        "Bun JavaScriptCore currently supports glibc Linux in JsRuntimeHost; musl requires Bun's separate musl archive")
                endif()

                if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
                   NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^21\\.")
                    message(FATAL_ERROR
                        "Bun's Linux JavaScriptCore LTO archives require Clang 21")
                endif()

                set(JSR_BUN_JSC_SAVED_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
                set(CMAKE_CXX_STANDARD 23)
                check_cxx_source_compiles([=[
                    #include <expected>
                    int main() { std::expected<int, int> value{1}; return *value - 1; }
                ]=] JSR_BUN_JSC_HAS_STD_EXPECTED)
                set(CMAKE_CXX_STANDARD "${JSR_BUN_JSC_SAVED_CXX_STANDARD}")
                if(NOT JSR_BUN_JSC_HAS_STD_EXPECTED)
                    message(FATAL_ERROR
                        "Bun's current WebKit headers require a C++23 standard library with <expected> (install libstdc++-12-dev on Ubuntu 22.04)")
                endif()

                find_program(JSR_BUN_JSC_LLD NAMES ld.lld-21 ld.lld REQUIRED)
                execute_process(
                    COMMAND "${JSR_BUN_JSC_LLD}" --version
                    OUTPUT_VARIABLE JSR_BUN_JSC_LLD_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
                if(NOT JSR_BUN_JSC_LLD_VERSION MATCHES "LLD 21\\.")
                    message(FATAL_ERROR
                        "Bun's Linux JavaScriptCore LTO archives require LLD 21; found '${JSR_BUN_JSC_LLD_VERSION}'")
                endif()

                string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" JSR_BUN_JSC_PROCESSOR)
                if(JSR_BUN_JSC_PROCESSOR MATCHES "^(x86_64|amd64)$")
                    # Use Bun's Nehalem-baseline full-LTO variant rather than imposing Haswell on
                    # every JsRuntimeHost Linux consumer.
                    set(JSR_BUN_JSC_ASSET "bun-webkit-linux-amd64-baseline-lto.tar.gz")
                    set(JSR_BUN_JSC_SHA256 "2756c59963525ee01f8878c07d5bceff98f6e926ccbe79dff52dfe44f4a0b5c3")
                elseif(JSR_BUN_JSC_PROCESSOR MATCHES "^(aarch64|arm64)$")
                    set(JSR_BUN_JSC_ASSET "bun-webkit-linux-arm64-lto.tar.gz")
                    set(JSR_BUN_JSC_SHA256 "5739c4e6b32258f30db22aeeddefe9dfa3b05d6edb5ea92985b8fbefa85a2cfe")
                else()
                    message(FATAL_ERROR
                        "Bun Linux JavaScriptCore supports only x86_64 and arm64; got '${CMAKE_SYSTEM_PROCESSOR}'")
                endif()
                set(JSR_BUN_JSC_OUTPUT_NAME jsruntimehost-jsc)
            endif()

            if(JSR_BUN_JSC_ROOT)
                set(BUN_JSC_DIR "${JSR_BUN_JSC_ROOT}")
            else()
                string(REGEX REPLACE "\\.tar\\.gz$" "" JSR_BUN_JSC_ASSET_STEM "${JSR_BUN_JSC_ASSET}")
                set(JSR_BUN_JSC_FETCH_DIR
                    "${CMAKE_CURRENT_BINARY_DIR}/${JSR_BUN_JSC_WEBKIT_REVISION}/${JSR_BUN_JSC_ASSET_STEM}")
                set(BUN_JSC_DIR "${JSR_BUN_JSC_FETCH_DIR}/bun-webkit")
                set(JSR_BUN_JSC_ARCHIVE "${JSR_BUN_JSC_FETCH_DIR}/${JSR_BUN_JSC_ASSET}")
                set(JSR_BUN_JSC_COMPLETE_STAMP
                    "${BUN_JSC_DIR}/.jsruntimehost-${JSR_BUN_JSC_SHA256}.complete")

                if(NOT EXISTS "${JSR_BUN_JSC_COMPLETE_STAMP}")
                    file(REMOVE_RECURSE "${BUN_JSC_DIR}")
                    file(MAKE_DIRECTORY "${JSR_BUN_JSC_FETCH_DIR}")
                    message(STATUS
                        "Downloading Bun WebKit ${JSR_BUN_JSC_WEBKIT_REVISION}: ${JSR_BUN_JSC_ASSET}")
                    file(DOWNLOAD
                        "https://github.com/oven-sh/WebKit/releases/download/${JSR_BUN_JSC_WEBKIT_RELEASE}/${JSR_BUN_JSC_ASSET}"
                        "${JSR_BUN_JSC_ARCHIVE}"
                        EXPECTED_HASH "SHA256=${JSR_BUN_JSC_SHA256}"
                        TLS_VERIFY ON
                        SHOW_PROGRESS
                        STATUS JSR_BUN_JSC_DOWNLOAD_STATUS)
                    list(GET JSR_BUN_JSC_DOWNLOAD_STATUS 0 JSR_BUN_JSC_DOWNLOAD_CODE)
                    if(NOT JSR_BUN_JSC_DOWNLOAD_CODE EQUAL 0)
                        list(GET JSR_BUN_JSC_DOWNLOAD_STATUS 1 JSR_BUN_JSC_DOWNLOAD_MESSAGE)
                        message(FATAL_ERROR "Unable to download Bun WebKit: ${JSR_BUN_JSC_DOWNLOAD_MESSAGE}")
                    endif()
                    file(ARCHIVE_EXTRACT
                        INPUT "${JSR_BUN_JSC_ARCHIVE}"
                        DESTINATION "${JSR_BUN_JSC_FETCH_DIR}"
                        PATTERNS
                            "bun-webkit/include"
                            "bun-webkit/lib/libJavaScriptCore.a"
                            "bun-webkit/lib/libWTF.a"
                            "bun-webkit/lib/libbmalloc.a"
                            "bun-webkit/lib/libicui18n.a"
                            "bun-webkit/lib/libicuuc.a"
                            "bun-webkit/lib/libicudata.a"
                            "bun-webkit/package.json")

                    foreach(JSR_BUN_JSC_EXTRACTED_FILE
                            "${BUN_JSC_DIR}/include/JavaScriptCore/JavaScript.h"
                            "${BUN_JSC_DIR}/lib/libJavaScriptCore.a"
                            "${BUN_JSC_DIR}/lib/libWTF.a"
                            "${BUN_JSC_DIR}/lib/libbmalloc.a"
                            "${BUN_JSC_DIR}/lib/libicui18n.a"
                            "${BUN_JSC_DIR}/lib/libicuuc.a"
                            "${BUN_JSC_DIR}/lib/libicudata.a")
                        if(NOT EXISTS "${JSR_BUN_JSC_EXTRACTED_FILE}")
                            message(FATAL_ERROR
                                "Bun WebKit extraction is missing '${JSR_BUN_JSC_EXTRACTED_FILE}'")
                        endif()
                    endforeach()

                    file(WRITE "${JSR_BUN_JSC_COMPLETE_STAMP}"
                        "${JSR_BUN_JSC_WEBKIT_REVISION}\n${JSR_BUN_JSC_SHA256}\n")
                    file(REMOVE "${JSR_BUN_JSC_ARCHIVE}")
                endif()
            endif()

            set(BUN_JSC_STATIC_LIBRARIES
                "${BUN_JSC_DIR}/lib/libJavaScriptCore.a"
                "${BUN_JSC_DIR}/lib/libWTF.a"
                "${BUN_JSC_DIR}/lib/libbmalloc.a"
                "${BUN_JSC_DIR}/lib/libicui18n.a"
                "${BUN_JSC_DIR}/lib/libicuuc.a"
                "${BUN_JSC_DIR}/lib/libicudata.a")

            foreach(BUN_JSC_FILE
                    "${BUN_JSC_DIR}/include/JavaScriptCore/JavaScript.h"
                    ${BUN_JSC_STATIC_LIBRARIES})
                if(NOT EXISTS "${BUN_JSC_FILE}")
                    message(FATAL_ERROR "Bun WebKit archive is missing '${BUN_JSC_FILE}'")
                endif()
            endforeach()

            set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
                PUBLIC "${BUN_JSC_DIR}/include")

            if(ANDROID)
                add_library(bun_jsc SHARED "Source/jsc_bun.cpp")
            else()
                # Bun's Linux LTO archives contain local-exec TLS relocations and are intended to
                # be linked into the initial executable. They cannot be repackaged into a shared
                # object, so keep this wrapper static and propagate the archives to the final link.
                add_library(bun_jsc STATIC "Source/jsc_bun.cpp")
            endif()
            set_target_properties(bun_jsc PROPERTIES
                OUTPUT_NAME "${JSR_BUN_JSC_OUTPUT_NAME}"
                CXX_STANDARD 23
                CXX_STANDARD_REQUIRED ON)
            target_compile_definitions(bun_jsc PRIVATE
                JSR_BUN_JSC_WEBKIT_REVISION="${JSR_BUN_JSC_WEBKIT_REVISION}"
                BUILDING_JSCONLY__
                BUILDING_JavaScriptCore
                BUILDING_WEBKIT=1
                BUILDING_WITH_CMAKE=1
                HAVE_CONFIG_H=1
                PAS_BMALLOC=1
                STATICALLY_LINKED_WITH_WTF
                STATICALLY_LINKED_WITH_bmalloc
                U_STATIC_IMPLEMENTATION=1)
            target_include_directories(bun_jsc PUBLIC "${BUN_JSC_DIR}/include")

            if(ANDROID)
                set_target_properties(bun_jsc PROPERTIES
                    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/jsc_bun.exports")
                target_link_options(bun_jsc PRIVATE
                    "-Wl,--no-undefined"
                    "-Wl,-Bsymbolic"
                    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/Source/jsc_bun.exports"
                    "-Wl,-z,max-page-size=16384"
                    "-Wl,-z,common-page-size=16384")
                target_link_libraries(bun_jsc PRIVATE
                    "-Wl,--whole-archive"
                    "${BUN_JSC_DIR}/lib/libJavaScriptCore.a"
                    "-Wl,--no-whole-archive"
                    "-Wl,--start-group"
                    "${BUN_JSC_DIR}/lib/libWTF.a"
                    "${BUN_JSC_DIR}/lib/libbmalloc.a"
                    "${BUN_JSC_DIR}/lib/libicui18n.a"
                    "${BUN_JSC_DIR}/lib/libicuuc.a"
                    "${BUN_JSC_DIR}/lib/libicudata.a"
                    "-Wl,--end-group"
                    log
                    ${CMAKE_DL_LIBS}
                    m)
            else()
                set_property(TARGET bun_jsc PROPERTY INTERFACE_POSITION_INDEPENDENT_CODE ON)
                target_compile_options(bun_jsc PRIVATE
                    -O2
                    -flto=full
                    -fwhole-program-vtables
                    -fforce-emit-vtables
                    -fno-sanitize=all)
                target_link_options(bun_jsc INTERFACE
                    -flto=full
                    -fwhole-program-vtables
                    -fforce-emit-vtables
                    -O2
                    -pie
                    "--ld-path=${JSR_BUN_JSC_LLD}"
                    "-Wl,--gc-sections")
                find_package(Threads REQUIRED)
                target_link_libraries(bun_jsc PUBLIC
                    "-Wl,--whole-archive"
                    "${BUN_JSC_DIR}/lib/libJavaScriptCore.a"
                    "-Wl,--no-whole-archive"
                    "-Wl,--start-group"
                    "${BUN_JSC_DIR}/lib/libWTF.a"
                    "${BUN_JSC_DIR}/lib/libbmalloc.a"
                    "${BUN_JSC_DIR}/lib/libicui18n.a"
                    "${BUN_JSC_DIR}/lib/libicuuc.a"
                    "${BUN_JSC_DIR}/lib/libicudata.a"
                    "-Wl,--end-group"
                    Threads::Threads
                    ${CMAKE_DL_LIBS}
                    m
                    atomic)
            endif()
            set_property(TARGET bun_jsc PROPERTY FOLDER Dependencies)
            set(JSR_USE_BUN_JSC TRUE)

            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC bun_jsc)
        elseif(APPLE)
            find_library(JAVASCRIPTCORE_LIBRARY JavaScriptCore)
            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC ${JAVASCRIPTCORE_LIBRARY})
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

if(JSR_USE_BUN_JSC)
    target_compile_definitions(napi PUBLIC JSR_USE_BUN_JSC=1)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        get_target_property(JSR_BUN_JSC_NAPI_TYPE napi TYPE)
        if(NOT JSR_BUN_JSC_NAPI_TYPE STREQUAL "STATIC_LIBRARY")
            message(FATAL_ERROR
                "Bun's Linux JavaScriptCore LTO archives must reach the final executable; build JsRuntimeHost static (BUILD_SHARED_LIBS=OFF)")
        endif()
    endif()
endif()

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
