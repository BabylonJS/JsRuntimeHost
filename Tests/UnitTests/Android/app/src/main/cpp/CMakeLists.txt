cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(UnitTestsJNI)

set(_jsruntimehost_runtime_source "")
set(_jsruntimehost_runtime_target "")

if(ANDROID)
    set(_jsruntimehost_sanitizers "")
    if(DEFINED ENV{JSRUNTIMEHOST_NATIVE_SANITIZERS})
        set(_jsruntimehost_sanitizers "$ENV{JSRUNTIMEHOST_NATIVE_SANITIZERS}")
    endif()
    if(DEFINED ENV{JSRUNTIMEHOST_ENABLE_ASAN})
        if(_jsruntimehost_sanitizers STREQUAL "")
            set(_jsruntimehost_sanitizers "address")
        else()
            set(_jsruntimehost_sanitizers "${_jsruntimehost_sanitizers},address")
        endif()
    endif()
    if(NOT _jsruntimehost_sanitizers STREQUAL "")
        string(REGEX REPLACE "[ \t\r\n]" "" _jsruntimehost_sanitizers "${_jsruntimehost_sanitizers}")
        string(REGEX REPLACE ",+" "," _jsruntimehost_sanitizers "${_jsruntimehost_sanitizers}")
        string(REGEX REPLACE "^,|,$" "" _jsruntimehost_sanitizers "${_jsruntimehost_sanitizers}")
        if(NOT _jsruntimehost_sanitizers STREQUAL "")
            message(STATUS "Enabling sanitizers: ${_jsruntimehost_sanitizers}")
            add_compile_options("-fsanitize=${_jsruntimehost_sanitizers}" "-fno-omit-frame-pointer")
            add_link_options("-fsanitize=${_jsruntimehost_sanitizers}")
            set(_jsruntimehost_sanitizers_list "${_jsruntimehost_sanitizers}")
            string(REPLACE "," ";" _jsruntimehost_sanitizers_list "${_jsruntimehost_sanitizers_list}")
            list(FIND _jsruntimehost_sanitizers_list "undefined" _jsruntimehost_has_ubsan)
            if(_jsruntimehost_has_ubsan GREATER -1)
                if(ANDROID_ABI STREQUAL "arm64-v8a")
                    set(_jsruntimehost_san_arch "aarch64")
                elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
                    set(_jsruntimehost_san_arch "arm")
                elseif(ANDROID_ABI STREQUAL "x86")
                    set(_jsruntimehost_san_arch "i686")
                elseif(ANDROID_ABI STREQUAL "x86_64")
                    set(_jsruntimehost_san_arch "x86_64")
                else()
                    set(_jsruntimehost_san_arch "")
                endif()
                if(_jsruntimehost_san_arch)
                    get_filename_component(_jsruntimehost_toolchain_dir "${CMAKE_C_COMPILER}" DIRECTORY)
                    get_filename_component(_jsruntimehost_toolchain_root "${_jsruntimehost_toolchain_dir}" DIRECTORY)
                    file(GLOB _jsruntimehost_ubsan_runtime
                        "${_jsruntimehost_toolchain_root}/lib/clang/*/lib/linux/libclang_rt.ubsan_standalone-${_jsruntimehost_san_arch}-android.so")
                    list(LENGTH _jsruntimehost_ubsan_runtime _jsruntimehost_ubsan_runtime_len)
                    if(_jsruntimehost_ubsan_runtime_len GREATER 0)
                        list(GET _jsruntimehost_ubsan_runtime 0 _jsruntimehost_ubsan_runtime_path)
                        set(_jsruntimehost_runtime_source "${_jsruntimehost_ubsan_runtime_path}")
                        set(_jsruntimehost_runtime_target "libclang_rt.ubsan_standalone-${_jsruntimehost_san_arch}-android.so")
                    else()
                        message(WARNING "UBSan runtime not found for ABI ${ANDROID_ABI}")
                    endif()
                endif()
            endif()
        endif()
    endif()
endif()

get_filename_component(UNIT_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(TESTS_DIR "${UNIT_TESTS_DIR}/.." ABSOLUTE)
get_filename_component(REPO_ROOT_DIR "${TESTS_DIR}/.." ABSOLUTE)

add_subdirectory(${REPO_ROOT_DIR} "${CMAKE_CURRENT_BINARY_DIR}/JsRuntimeHost")

FetchContent_MakeAvailable_With_Message(googletest)

npm(install --silent WORKING_DIRECTORY ${TESTS_DIR})

add_library(UnitTestsJNI SHARED
    JNI.cpp
    ${UNIT_TESTS_DIR}/Shared/Shared.h
    ${UNIT_TESTS_DIR}/Shared/Shared.cpp)

target_compile_definitions(UnitTestsJNI PRIVATE JSRUNTIMEHOST_PLATFORM="${JSRUNTIMEHOST_PLATFORM}")

target_include_directories(UnitTestsJNI
    PRIVATE ${UNIT_TESTS_DIR})

target_link_libraries(UnitTestsJNI
    PRIVATE log
    PRIVATE AndroidExtensions
    PRIVATE AppRuntime
    PRIVATE AbortController
    PRIVATE Console
    PRIVATE Scheduling
    PRIVATE ScriptLoader
    PRIVATE URL
    PRIVATE UrlLib
    PRIVATE XMLHttpRequest
    PRIVATE WebSocket
    PRIVATE gtest_main
    PRIVATE Blob)

if(_jsruntimehost_runtime_source AND _jsruntimehost_runtime_target)
    add_custom_command(TARGET UnitTestsJNI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_jsruntimehost_runtime_source}"
            "$<TARGET_FILE_DIR:UnitTestsJNI>/${_jsruntimehost_runtime_target}")
endif()
