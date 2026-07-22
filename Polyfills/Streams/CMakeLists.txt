set(WEB_STREAMS_POLYFILL_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/web-streams-polyfill/ponyfill.es5.js")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${WEB_STREAMS_POLYFILL_FILE}")
file(READ "${WEB_STREAMS_POLYFILL_FILE}" WEB_STREAMS_POLYFILL_SOURCE)
string(SUBSTRING "${WEB_STREAMS_POLYFILL_SOURCE}" 0 60000 WEB_STREAMS_POLYFILL_SOURCE_1)
string(SUBSTRING "${WEB_STREAMS_POLYFILL_SOURCE}" 60000 -1 WEB_STREAMS_POLYFILL_SOURCE_2)
configure_file(
    "Source/StreamsScripts.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/StreamsScripts.h"
    @ONLY)

set(SOURCES
    "Include/Babylon/Polyfills/Streams.h"
    "Source/Streams.cpp"
    "Source/StreamsScripts.h.in"
    "ThirdParty/web-streams-polyfill/LICENSE"
    "ThirdParty/web-streams-polyfill/ponyfill.es5.js")

add_library(Streams ${SOURCES})
warnings_as_errors(Streams)

target_include_directories(Streams
    PUBLIC "Include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

target_link_libraries(Streams PUBLIC JsRuntime)

set_property(TARGET Streams PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
