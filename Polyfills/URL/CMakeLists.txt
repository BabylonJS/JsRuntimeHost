set(SOURCES 
    ${SOURCES}
    "Include/Babylon/Polyfills/URL.h"
    "Source/URL.cpp"
    "Source/URL.h")

add_library(URL ${SOURCES})
#warnings_as_errors(URL)

target_include_directories(URL PUBLIC "Include")

target_link_libraries(URL
    PUBLIC JsRuntime
    PRIVATE arcana)

set_property(TARGET URL PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
