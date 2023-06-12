set(SOURCES
    "Include/Babylon/ScriptLoader.h"
    "Source/ScriptLoader.cpp")

add_library(ScriptLoader ${SOURCES})
# unused parameters in v8.h
if(NOT NAPI_JAVASCRIPT_ENGINE STREQUAL "V8")
    warnings_as_errors(ScriptLoader)
endif()

target_include_directories(ScriptLoader PUBLIC "Include")

target_link_libraries(ScriptLoader
    PUBLIC napi
    PRIVATE arcana
    PRIVATE UrlLib)

set_property(TARGET ScriptLoader PROPERTY FOLDER Core)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
