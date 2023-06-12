set(SOURCES 
    "Include/Babylon/Polyfills/AbortController.h"
    "Source/AbortController.cpp"
    "Source/AbortController.h"
    "Source/AbortSignal.cpp"
    "Source/AbortSignal.h")

add_library(AbortController ${SOURCES})
# unused parameters in v8.h
if(NOT NAPI_JAVASCRIPT_ENGINE STREQUAL "V8")
    warnings_as_errors(AbortController)
endif()

target_include_directories(AbortController PUBLIC "Include")

target_link_libraries(AbortController
    PUBLIC JsRuntime)

set_property(TARGET AbortController PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
