set(SOURCES 
    "Include/Babylon/Polyfills/XMLHttpRequest.h"
    "Source/XMLHttpRequest.cpp"
    "Source/XMLHttpRequest.h")

add_library(XMLHttpRequest ${SOURCES})
# unused parameters in v8.h
if(NOT NAPI_JAVASCRIPT_ENGINE STREQUAL "V8")
    warnings_as_errors(XMLHttpRequest)
endif()

target_include_directories(XMLHttpRequest PUBLIC "Include")

target_link_libraries(XMLHttpRequest
    PUBLIC JsRuntime
    PRIVATE arcana
    PRIVATE UrlLib)

set_property(TARGET XMLHttpRequest PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
