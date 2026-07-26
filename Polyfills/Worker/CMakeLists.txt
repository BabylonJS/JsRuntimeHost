set(SOURCES
    "Include/Babylon/Polyfills/Worker.h"
    "Source/Worker.cpp"
    "Source/Worker.h"
    "Source/WorkerScripts.h")

add_library(Worker ${SOURCES})
warnings_as_errors(Worker)

target_include_directories(Worker PUBLIC "Include")

find_package(ZLIB QUIET)
if(TARGET ZLIB::ZLIB)
    set(JSRUNTIMEHOST_WORKER_ZLIB_TARGET ZLIB::ZLIB)
else()
    # Keep Worker portable on platforms without a packaged zlib (notably
    # fresh Windows hosts). The project already uses FetchContent for its
    # pinned dependencies, so use the same reproducible fallback.
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable_With_Message(zlib)
    if(TARGET zlibstatic)
        set(JSRUNTIMEHOST_WORKER_ZLIB_TARGET zlibstatic)
    elseif(TARGET zlib)
        set(JSRUNTIMEHOST_WORKER_ZLIB_TARGET zlib)
    else()
        message(FATAL_ERROR "Worker requires zlib for DecompressionStream")
    endif()
endif()

target_link_libraries(Worker
    PUBLIC AppRuntime
    PRIVATE AbortController
    PRIVATE Blob
    PRIVATE Console
    PRIVATE Fetch
    PRIVATE File
    PRIVATE Performance
    PRIVATE Scheduling
    PRIVATE TextDecoder
    PRIVATE TextEncoder
    PRIVATE URL
    PRIVATE WebSocket
    PRIVATE XMLHttpRequest
    PRIVATE ${JSRUNTIMEHOST_WORKER_ZLIB_TARGET})

set_property(TARGET Worker PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
