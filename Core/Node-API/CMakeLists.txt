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
    "Include/Shared/napi/napi-inl.h"
    "Source/env.cc")

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

    if(NAPI_JAVASCRIPT_ENGINE STREQUAL "Chakra")
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
            set(V8_PACKAGE_NAME "jsc-android")
            set(JSC_ANDROID_DIR "${CMAKE_CURRENT_BINARY_DIR}/${V8_PACKAGE_NAME}")
            napi_install_android_package(jsc "dist/org/webkit/android-jsc" ${JSC_ANDROID_DIR})

            # Add `JavaScriptCore` prefix to the include path
            file(RENAME "${JSC_ANDROID_DIR}/include" "${JSC_ANDROID_DIR}/JavaScriptCore")
            file(MAKE_DIRECTORY "${JSC_ANDROID_DIR}/include")
            file(RENAME "${JSC_ANDROID_DIR}/JavaScriptCore" "${JSC_ANDROID_DIR}/include/JavaScriptCore")

            set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
                PUBLIC "${JSC_ANDROID_DIR}/include")

            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC "${JSC_ANDROID_DIR}/jni/${ANDROID_ABI}/libjsc.so")
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
            if(NOT COMMAND _jsruntimehost_v8_log)
                macro(_jsruntimehost_v8_log message_text)
                    message(STATUS "[JSRUNTIMEHOST_V8] ${message_text}")
                endmacro()
            endif()

            # Derive additional Android-specific metadata used to locate
            # system-provided V8 headers and libraries.
            set(_v8_android_requested_api "")
            if(DEFINED JSRUNTIMEHOST_ANDROID_TARGET_API)
                set(_v8_android_requested_api "${JSRUNTIMEHOST_ANDROID_TARGET_API}")
            endif()

            set(_v8_android_platform_level "")
            if(DEFINED ANDROID_PLATFORM)
                string(REGEX REPLACE "^android-" "" _v8_android_platform_level "${ANDROID_PLATFORM}")
            endif()

            if(_v8_android_requested_api)
                if(_v8_android_platform_level AND NOT _v8_android_platform_level STREQUAL _v8_android_requested_api)
                    _jsruntimehost_v8_log("Forcing Android native API level ${_v8_android_requested_api} (was ${_v8_android_platform_level})")
                elseif(NOT _v8_android_platform_level)
                    _jsruntimehost_v8_log("Configuring Android native API level ${_v8_android_requested_api}")
                endif()

                set(_v8_android_platform_level "${_v8_android_requested_api}")
                set(_v8_android_platform_value "android-${_v8_android_platform_level}")
                set(ANDROID_PLATFORM "${_v8_android_platform_value}" CACHE STRING "Android platform level" FORCE)
                set(CMAKE_SYSTEM_VERSION "${_v8_android_platform_level}" CACHE STRING "Android system version" FORCE)
            endif()

            set(_v8_android_api_level "${_v8_android_platform_level}")
            if(NOT _v8_android_api_level AND DEFINED CMAKE_SYSTEM_VERSION)
                set(_v8_android_api_level "${CMAKE_SYSTEM_VERSION}")
            endif()
            if(NOT _v8_android_api_level AND _v8_android_requested_api)
                set(_v8_android_api_level "${_v8_android_requested_api}")
            endif()

            set(_v8_android_ndk_root "")
            if(CMAKE_ANDROID_NDK)
                set(_v8_android_ndk_root "${CMAKE_ANDROID_NDK}")
            elseif(ANDROID_NDK)
                set(_v8_android_ndk_root "${ANDROID_NDK}")
            endif()
            if(_v8_android_ndk_root)
                file(REAL_PATH "${_v8_android_ndk_root}" _v8_android_ndk_root)
                _jsruntimehost_v8_log("NDK root: ${_v8_android_ndk_root}")

                set(_v8_android_ndk_version "")
                if(EXISTS "${_v8_android_ndk_root}/source.properties")
                    file(STRINGS "${_v8_android_ndk_root}/source.properties" _v8_ndk_props REGEX "^Pkg.Revision = ")
                    if(_v8_ndk_props)
                        list(GET _v8_ndk_props 0 _v8_ndk_props_line)
                        string(REGEX REPLACE "^Pkg.Revision = " "" _v8_android_ndk_version "${_v8_ndk_props_line}")
                    endif()
                endif()

                if(_v8_android_ndk_version)
                    string(STRIP "${_v8_android_ndk_version}" _v8_android_ndk_version)
                    _jsruntimehost_v8_log("NDK version: ${_v8_android_ndk_version}")
                    if(_v8_android_ndk_version VERSION_LESS "28.2")
                        message(FATAL_ERROR "JsRuntimeHost requires Android NDK version 28.2 or newer but found ${_v8_android_ndk_version} at ${_v8_android_ndk_root}")
                    endif()
                else()
                    _jsruntimehost_v8_log("NDK version: <unknown>")
                endif()
            else()
                _jsruntimehost_v8_log("NDK root: <unknown>")
            endif()

            set(_v8_android_triple "")
            set(_v8_android_is_64bit OFF)
            if(ANDROID_ABI STREQUAL "arm64-v8a")
                set(_v8_android_triple "aarch64-linux-android")
                set(_v8_android_is_64bit ON)
            elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
                set(_v8_android_triple "arm-linux-androideabi")
            elseif(ANDROID_ABI STREQUAL "x86_64")
                set(_v8_android_triple "x86_64-linux-android")
                set(_v8_android_is_64bit ON)
            elseif(ANDROID_ABI STREQUAL "x86")
                set(_v8_android_triple "i686-linux-android")
            endif()

            if(_v8_android_api_level)
                _jsruntimehost_v8_log("Target API level: ${_v8_android_api_level}")
            else()
                _jsruntimehost_v8_log("Target API level: <unspecified>")
            endif()

            if(_v8_android_triple)
                _jsruntimehost_v8_log("ABI: ${ANDROID_ABI} (${_v8_android_triple})")
            else()
                _jsruntimehost_v8_log("ABI: ${ANDROID_ABI} (no triple match)")
            endif()

            set(_v8_android_sdk_roots)
            if(ANDROID_SDK_ROOT)
                list(APPEND _v8_android_sdk_roots "${ANDROID_SDK_ROOT}")
            endif()
            if(DEFINED ENV{ANDROID_SDK_ROOT})
                list(APPEND _v8_android_sdk_roots "$ENV{ANDROID_SDK_ROOT}")
            endif()
            if(DEFINED ENV{ANDROID_HOME})
                list(APPEND _v8_android_sdk_roots "$ENV{ANDROID_HOME}")
            endif()
            list(REMOVE_DUPLICATES _v8_android_sdk_roots)

            if(_v8_android_sdk_roots)
                foreach(_v8_sdk_root IN LISTS _v8_android_sdk_roots)
                    _jsruntimehost_v8_log("SDK root hint: ${_v8_sdk_root}")
                endforeach()
            else()
                _jsruntimehost_v8_log("SDK root hint: <none>")
            endif()

            set(_v8_android_library_hints)
            set(_v8_android_header_hints)

            set(_v8_android_sysroots)
            if(CMAKE_SYSROOT)
                list(APPEND _v8_android_sysroots "${CMAKE_SYSROOT}")
            endif()
            if(DEFINED CMAKE_ANDROID_NDK_TOOLCHAIN_ROOT)
                list(APPEND _v8_android_sysroots "${CMAKE_ANDROID_NDK_TOOLCHAIN_ROOT}/sysroot")
            endif()
            if(CMAKE_ANDROID_NDK)
                file(GLOB _v8_ndk_prebuilts "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/*/sysroot")
                list(APPEND _v8_android_sysroots ${_v8_ndk_prebuilts})
            endif()
            if(ANDROID_NDK)
                file(GLOB _v8_env_ndk_prebuilts "${ANDROID_NDK}/toolchains/llvm/prebuilt/*/sysroot")
                list(APPEND _v8_android_sysroots ${_v8_env_ndk_prebuilts})
            endif()
            if(DEFINED ENV{ANDROID_NDK_ROOT})
                file(GLOB _v8_env_root_prebuilts "$ENV{ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/*/sysroot")
                list(APPEND _v8_android_sysroots ${_v8_env_root_prebuilts})
            endif()
            list(REMOVE_DUPLICATES _v8_android_sysroots)

            if(_v8_android_sysroots)
                foreach(_v8_sysroot IN LISTS _v8_android_sysroots)
                    _jsruntimehost_v8_log("Sysroot hint: ${_v8_sysroot}")
                endforeach()
            else()
                _jsruntimehost_v8_log("Sysroot hint: <none>")
            endif()

            foreach(_v8_sysroot IN LISTS _v8_android_sysroots)
                if(NOT _v8_sysroot)
                    continue()
                endif()

                list(APPEND _v8_android_library_hints
                    "${_v8_sysroot}/usr/lib"
                    "${_v8_sysroot}/usr/lib32"
                    "${_v8_sysroot}/usr/lib64"
                    "${_v8_sysroot}/usr/lib/${ANDROID_ABI}"
                    "${_v8_sysroot}/usr/lib/${_v8_android_triple}"
                    "${_v8_sysroot}/system/lib"
                    "${_v8_sysroot}/system/lib64")

                if(_v8_android_api_level)
                    list(APPEND _v8_android_library_hints
                        "${_v8_sysroot}/usr/lib/${_v8_android_triple}/${_v8_android_api_level}"
                        "${_v8_sysroot}/usr/lib/${_v8_android_triple}/${_v8_android_api_level}/lib"
                        "${_v8_sysroot}/usr/lib/${_v8_android_triple}/${_v8_android_api_level}/lib64")
                endif()

                list(APPEND _v8_android_library_hints
                    "${_v8_sysroot}/apex/com.android.vndk.current/lib")
                if(_v8_android_is_64bit)
                    list(APPEND _v8_android_library_hints
                        "${_v8_sysroot}/apex/com.android.vndk.current/lib64")
                endif()

                if(_v8_android_api_level)
                    list(APPEND _v8_android_library_hints
                        "${_v8_sysroot}/apex/com.android.vndk.v${_v8_android_api_level}/lib")
                    if(_v8_android_is_64bit)
                        list(APPEND _v8_android_library_hints
                            "${_v8_sysroot}/apex/com.android.vndk.v${_v8_android_api_level}/lib64")
                    endif()
                endif()

                list(APPEND _v8_android_library_hints
                    "${_v8_sysroot}/apex/com.android.appsearch/lib"
                    "${_v8_sysroot}/apex/com.android.xr.runtime/lib")
                if(_v8_android_is_64bit)
                    list(APPEND _v8_android_library_hints
                        "${_v8_sysroot}/apex/com.android.appsearch/lib64"
                        "${_v8_sysroot}/apex/com.android.xr.runtime/lib64")
                endif()

                list(APPEND _v8_android_header_hints
                    "${_v8_sysroot}"
                    "${_v8_sysroot}/usr"
                    "${_v8_sysroot}/usr/include"
                    "${_v8_sysroot}/usr/include/v8"
                    "${_v8_sysroot}/usr/include/libv8android"
                    "${_v8_sysroot}/usr/include/libv8android/include"
                    "${_v8_sysroot}/usr/include/chromium"
                    "${_v8_sysroot}/usr/include/chromium/libv8android"
                    "${_v8_sysroot}/usr/include/chromium/libv8android/include"
                    "${_v8_sysroot}/usr/include/libv8android/public"
                    "${_v8_sysroot}/usr/include/libv8android/public/include"
                    "${_v8_sysroot}/usr/include/chromium/libv8android/public"
                    "${_v8_sysroot}/usr/include/chromium/libv8android/public/include"
                    "${_v8_sysroot}/usr/local/include"
                    "${_v8_sysroot}/apex/com.android.vndk.current/include"
                    "${_v8_sysroot}/apex/com.android.vndk.current/include/libv8android"
                    "${_v8_sysroot}/apex/com.android.vndk.current/include/libv8android/include")

                if(_v8_android_api_level)
                    list(APPEND _v8_android_header_hints
                        "${_v8_sysroot}/apex/com.android.vndk.v${_v8_android_api_level}/include"
                        "${_v8_sysroot}/apex/com.android.vndk.v${_v8_android_api_level}/include/libv8android"
                        "${_v8_sysroot}/apex/com.android.vndk.v${_v8_android_api_level}/include/libv8android/include")
                endif()

                list(APPEND _v8_android_header_hints
                    "${_v8_sysroot}/apex/com.android.appsearch/include"
                    "${_v8_sysroot}/apex/com.android.appsearch/include/libv8android"
                    "${_v8_sysroot}/apex/com.android.appsearch/include/libv8android/include"
                    "${_v8_sysroot}/apex/com.android.xr.runtime/include"
                    "${_v8_sysroot}/apex/com.android.xr.runtime/include/libv8android"
                    "${_v8_sysroot}/apex/com.android.xr.runtime/include/libv8android/include"
                    "${_v8_sysroot}/apex/com.android.xr.runtime/include/libv8android/public"
                    "${_v8_sysroot}/apex/com.android.xr.runtime/include/libv8android/public/include")
            endforeach()

            foreach(_v8_sdk_root IN LISTS _v8_android_sdk_roots)
                if(NOT _v8_sdk_root)
                    continue()
                endif()

                if(_v8_android_api_level)
                    list(APPEND _v8_android_header_hints
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/include"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/include"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/ndk/include"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/public/include"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/sdk/include")

                    list(APPEND _v8_android_library_hints
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/lib"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/lib64"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/lib"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/lib64"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/lib/${_v8_android_triple}"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/libs/${ANDROID_ABI}"
                        "${_v8_sdk_root}/platforms/android-${_v8_android_api_level}/optional/libv8android/${ANDROID_ABI}")
                endif()
            endforeach()

            list(REMOVE_DUPLICATES _v8_android_library_hints)
            list(REMOVE_DUPLICATES _v8_android_header_hints)

            list(LENGTH _v8_android_library_hints _v8_library_hint_count)
            list(LENGTH _v8_android_header_hints _v8_header_hint_count)
            _jsruntimehost_v8_log("Collected ${_v8_library_hint_count} library hint(s)")
            _jsruntimehost_v8_log("Collected ${_v8_header_hint_count} header hint(s)")

            if(_v8_android_library_hints)
                foreach(_v8_hint IN LISTS _v8_android_library_hints)
                    _jsruntimehost_v8_log("Library search hint: ${_v8_hint}")
                endforeach()
            endif()

            if(_v8_android_header_hints)
                foreach(_v8_hint IN LISTS _v8_android_header_hints)
                    _jsruntimehost_v8_log("Header search hint: ${_v8_hint}")
                endforeach()
            endif()

            if(NOT V8_ANDROID_LIBRARY)
                find_library(V8_ANDROID_LIBRARY
                    NAMES v8android
                    HINTS ${_v8_android_library_hints})
            endif()

            if(V8_ANDROID_LIBRARY)
                _jsruntimehost_v8_log("Resolved libv8android: ${V8_ANDROID_LIBRARY}")
                get_filename_component(_v8_library_dir "${V8_ANDROID_LIBRARY}" DIRECTORY)
                get_filename_component(_v8_usr_dir "${_v8_library_dir}" DIRECTORY)
                get_filename_component(_v8_root_dir "${_v8_usr_dir}" DIRECTORY)

                list(APPEND _v8_android_header_hints
                    "${_v8_library_dir}"
                    "${_v8_usr_dir}"
                    "${_v8_usr_dir}/include"
                    "${_v8_usr_dir}/include/v8"
                    "${_v8_usr_dir}/include/libv8android"
                    "${_v8_usr_dir}/include/libv8android/include"
                    "${_v8_root_dir}"
                    "${_v8_root_dir}/include"
                    "${_v8_root_dir}/usr/include"
                    "${_v8_root_dir}/usr/include/v8"
                    "${_v8_root_dir}/usr/include/libv8android"
                    "${_v8_root_dir}/usr/include/libv8android/include")
                list(REMOVE_DUPLICATES _v8_android_header_hints)
            else()
                _jsruntimehost_v8_log("libv8android not resolved from hints")
            endif()

            if(NOT V8_ANDROID_INCLUDE_DIR)
                find_path(V8_ANDROID_INCLUDE_DIR
                    NAMES v8.h
                    HINTS ${_v8_android_header_hints}
                    PATH_SUFFIXES
                        v8
                        include
                        include/v8
                        libv8android
                        libv8android/include
                        libv8android/public
                        libv8android/public/include
                        include/libv8android
                        include/libv8android/include
                        include/libv8android/public
                        include/libv8android/public/include
                        sdk/include
                        ndk/include)
            endif()

            if(NOT V8_ANDROID_INCLUDE_DIR)
                _jsruntimehost_v8_log("Falling back to recursive header search")
                set(_v8_header_scan_roots)
                foreach(_v8_hint IN LISTS _v8_android_header_hints)
                    if(_v8_hint)
                        get_filename_component(_v8_hint_parent "${_v8_hint}" ABSOLUTE)
                        list(APPEND _v8_header_scan_roots "${_v8_hint_parent}")
                    endif()
                endforeach()
                list(REMOVE_DUPLICATES _v8_header_scan_roots)

                foreach(_v8_scan_root IN LISTS _v8_header_scan_roots)
                    if(NOT EXISTS "${_v8_scan_root}")
                        _jsruntimehost_v8_log("Skipping missing header scan root: ${_v8_scan_root}")
                        continue()
                    endif()

                    _jsruntimehost_v8_log("Scanning for v8.h under: ${_v8_scan_root}")
                    file(GLOB_RECURSE _v8_found_headers FOLLOW_SYMLINKS LIST_DIRECTORIES false
                        "${_v8_scan_root}/v8.h")
                    if(_v8_found_headers)
                        list(SORT _v8_found_headers)
                        list(GET _v8_found_headers 0 _v8_first_header)
                        get_filename_component(V8_ANDROID_INCLUDE_DIR "${_v8_first_header}" DIRECTORY)
                        _jsruntimehost_v8_log("Resolved v8.h via recursive scan: ${V8_ANDROID_INCLUDE_DIR}")
                        break()
                    endif()
                endforeach()
                unset(_v8_found_headers)
                unset(_v8_header_scan_roots)
            endif()

            if(NOT V8_ANDROID_INCLUDE_DIR)
                _jsruntimehost_v8_log("Unable to resolve v8.h from collected hints")
                message(FATAL_ERROR "Unable to locate V8 headers for Android. Set V8_ANDROID_INCLUDE_DIR to the directory containing v8.h.")
            endif()

            _jsruntimehost_v8_log("Using V8 include directory: ${V8_ANDROID_INCLUDE_DIR}")

            if(NOT V8_ANDROID_LIBRARY)
                _jsruntimehost_v8_log("Unable to resolve libv8android from collected hints")
                message(FATAL_ERROR "Unable to locate the system libv8android.so. Set V8_ANDROID_LIBRARY to the path of the library.")
            endif()

            _jsruntimehost_v8_log("Using libv8android: ${V8_ANDROID_LIBRARY}")

            unset(_v8_android_sdk_roots)
            unset(_v8_android_library_hints)
            unset(_v8_android_header_hints)
            unset(_v8_android_sysroots)
            unset(_v8_ndk_prebuilts)
            unset(_v8_env_ndk_prebuilts)
            unset(_v8_env_root_prebuilts)
            unset(_v8_library_dir)
            unset(_v8_usr_dir)
            unset(_v8_root_dir)
            unset(_v8_android_api_level)
            unset(_v8_android_triple)
            unset(_v8_android_is_64bit)

            set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
                PUBLIC "${V8_ANDROID_INCLUDE_DIR}")

            set(LINK_LIBRARIES ${LINK_LIBRARIES}
                PUBLIC "${V8_ANDROID_LIBRARY}")
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
    else()
        message(FATAL_ERROR "Unsupported JavaScript engine: ${NAPI_JAVASCRIPT_ENGINE}")
    endif()

    message(STATUS "Selected ${NAPI_JAVASCRIPT_ENGINE}")
endif()

add_library(napi ${SOURCES})

target_include_directories(napi ${INCLUDE_DIRECTORIES})
target_link_libraries(napi ${LINK_LIBRARIES})

set_property(TARGET napi PROPERTY FOLDER Dependencies)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
