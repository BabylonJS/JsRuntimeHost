set(SOURCES
    "Include/Babylon/Polyfills/DynamicScriptLoader.h"
    "Source/DynamicScriptLoader.cpp")

add_library(DynamicScriptLoader ${SOURCES})
warnings_as_errors(DynamicScriptLoader)

target_include_directories(DynamicScriptLoader PUBLIC "Include")

target_link_libraries(DynamicScriptLoader
    PUBLIC Foundation
    PUBLIC napi)

set_property(TARGET DynamicScriptLoader PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
