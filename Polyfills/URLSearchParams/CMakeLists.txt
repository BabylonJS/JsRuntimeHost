set(SOURCES 
    ${SOURCES}
    "Include/Babylon/Polyfills/URLSearchParams.h"
    "Source/URLSearchParams.cpp"
    "Source/URLSearchParams.h")

add_library(URLSearchParams ${SOURCES})
#warnings_as_errors(URLSearchParams)

target_include_directories(URLSearchParams PUBLIC "Include")

target_link_libraries(URLSearchParams
    PUBLIC JsRuntime
    PRIVATE arcana)

set_property(TARGET URLSearchParams PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
