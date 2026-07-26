set(FETCH_POLYFILL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Source/FetchPolyfill.js")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FETCH_POLYFILL_FILE}")
file(READ "${FETCH_POLYFILL_FILE}" FETCH_POLYFILL_SOURCE)
configure_file(
    "Source/FetchScripts.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/FetchScripts.h"
    @ONLY)

set(SOURCES
    "Include/Babylon/Polyfills/Fetch.h"
    "Readme.md"
    "Source/Fetch.h"
    "Source/Fetch.cpp"
    "Source/FetchPolyfill.js"
    "Source/FetchScripts.h.in")

add_library(Fetch ${SOURCES})
warnings_as_errors(Fetch)

target_include_directories(Fetch
    PUBLIC "Include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

target_link_libraries(Fetch
    PUBLIC JsRuntime
    PRIVATE arcana
    PRIVATE UrlLib)

set_property(TARGET Fetch PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
