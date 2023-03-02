set(SOURCES 
    "Include/Babylon/Polyfills/AbortSignal.h"
    "Source/AbortSignal.cpp"
    "Source/AbortSignal.h")

add_library(AbortSignal ${SOURCES})
#warnings_as_errors(AbortSignal)

target_include_directories(AbortSignal PUBLIC "Include")

target_link_libraries(AbortSignal
    PUBLIC JsRuntime
    PRIVATE arcana)

set_property(TARGET AbortSignal PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
