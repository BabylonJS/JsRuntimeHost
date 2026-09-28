set(SOURCES
    "Include/Babylon/Polyfills/ImportScripts.h"
    "Source/ImportScripts.cpp")

add_library(ImportScripts ${SOURCES})
warnings_as_errors(ImportScripts)

target_include_directories(ImportScripts PUBLIC "Include")

target_link_libraries(ImportScripts
    PUBLIC Foundation
    PUBLIC napi)

set_property(TARGET ImportScripts PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
