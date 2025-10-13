cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(UnitTestsJNI)

get_filename_component(UNIT_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
get_filename_component(TESTS_DIR "${UNIT_TESTS_DIR}/.." ABSOLUTE)
get_filename_component(REPO_ROOT_DIR "${TESTS_DIR}/.." ABSOLUTE)

add_subdirectory(${REPO_ROOT_DIR} "${CMAKE_CURRENT_BINARY_DIR}/JsRuntimeHost")

FetchContent_MakeAvailable_With_Message(googletest)

npm(install --silent WORKING_DIRECTORY ${TESTS_DIR})

set(NODE_LITE_PLATFORM_SRC ${TESTS_DIR}/NodeApi/node_lite_posix.cpp)
set(NODE_LITE_CHILD_PROCESS_SRC ${TESTS_DIR}/NodeApi/child_process_posix.cpp)

if(WIN32)
    set(NODE_LITE_PLATFORM_SRC ${TESTS_DIR}/NodeApi/node_lite_windows.cpp)
    set(NODE_LITE_CHILD_PROCESS_SRC ${TESTS_DIR}/NodeApi/child_process.cpp)
elseif(APPLE)
    set(NODE_LITE_PLATFORM_SRC ${TESTS_DIR}/NodeApi/node_lite_mac.cpp)
    set(NODE_LITE_CHILD_PROCESS_SRC ${TESTS_DIR}/NodeApi/child_process_mac.cpp)
endif()

add_library(UnitTestsJNI SHARED
    JNI.cpp
    ${UNIT_TESTS_DIR}/Shared/Shared.h
    ${UNIT_TESTS_DIR}/Shared/Shared.cpp
    ${TESTS_DIR}/NodeApi/node_lite.cpp
    ${NODE_LITE_PLATFORM_SRC}
    ${NODE_LITE_CHILD_PROCESS_SRC}
    ${TESTS_DIR}/NodeApi/node_lite_jsruntimehost.cpp
    ${TESTS_DIR}/NodeApi/js_runtime_api.cpp
    ${TESTS_DIR}/NodeApi/string_utils.cpp
    ${TESTS_DIR}/NodeApi/test_main.cpp)

target_compile_definitions(UnitTestsJNI PRIVATE JSRUNTIMEHOST_PLATFORM="${JSRUNTIMEHOST_PLATFORM}")

target_include_directories(UnitTestsJNI
    PRIVATE ${UNIT_TESTS_DIR}
    PRIVATE ${TESTS_DIR}/NodeApi
    PRIVATE ${TESTS_DIR}/NodeApi/include
    PRIVATE ${REPO_ROOT_DIR}/Core/Node-API/Source)

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
    PRIVATE Blob
    PRIVATE napi)

if(ANDROID)
    target_link_libraries(UnitTestsJNI PRIVATE dl)
endif()
target_compile_definitions(UnitTestsJNI
    PRIVATE NODE_API_AVAILABLE_NATIVE_TESTS="2_function_arguments,3_callbacks,4_object_factory,5_function_factory")
