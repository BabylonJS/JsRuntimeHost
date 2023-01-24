cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project("UnitTestsJNI")

get_filename_component(REPO_ROOT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../.."
    ABSOLUTE)

# Turn off the tests folder for Android Studio path
set(JSRUNTIMEHOST_TESTS OFF)

add_subdirectory(${REPO_ROOT_DIR} "${CMAKE_CURRENT_BINARY_DIR}/JsRuntimeHost")

add_library(UnitTestsJNI SHARED
    JNI.cpp
    ../../../../../Shared/Shared.h
    ../../../../../Shared/Shared.cpp)

target_include_directories(UnitTestsJNI
    PRIVATE ../../../../..)

target_link_libraries(UnitTestsJNI
    PRIVATE log
    PRIVATE AndroidExtensions
    PRIVATE AppRuntime
    PRIVATE Console
    PRIVATE Scheduling
    PRIVATE ScriptLoader
    PRIVATE UrlLib
    PRIVATE XMLHttpRequest)
