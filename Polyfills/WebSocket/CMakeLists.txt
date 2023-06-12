set(SOURCES 
    ${SOURCES}
    "Include/Babylon/Polyfills/WebSocket.h"
    "Source/WebSocket.cpp"
    "Source/WebSocket.h")

add_library(WebSocket ${SOURCES})
# unused parameters in v8.h
if(NOT NAPI_JAVASCRIPT_ENGINE STREQUAL "V8")
    warnings_as_errors(WebSocket)
endif()

target_include_directories(WebSocket PUBLIC "Include")

target_link_libraries(WebSocket
    PUBLIC JsRuntime
    PRIVATE arcana
    PRIVATE UrlLib)

set_property(TARGET WebSocket PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
