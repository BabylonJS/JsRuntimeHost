set(INDEXEDDB_POLYFILL_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/fake-indexeddb/fake-indexeddb.js")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${INDEXEDDB_POLYFILL_FILE}")
file(READ "${INDEXEDDB_POLYFILL_FILE}" INDEXEDDB_POLYFILL_SOURCE)
string(SUBSTRING "${INDEXEDDB_POLYFILL_SOURCE}" 0 60000 INDEXEDDB_POLYFILL_SOURCE_1)
string(SUBSTRING "${INDEXEDDB_POLYFILL_SOURCE}" 60000 -1 INDEXEDDB_POLYFILL_SOURCE_2)
configure_file(
    "Source/IndexedDBScripts.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/IndexedDBScripts.h"
    @ONLY)

set(SOURCES
    "Include/Babylon/Polyfills/IndexedDB.h"
    "Source/IndexedDB.cpp"
    "Source/IndexedDBScripts.h.in"
    "ThirdParty/fake-indexeddb/BuildEntry.js"
    "ThirdParty/fake-indexeddb/StorageClone.js"
    "ThirdParty/fake-indexeddb/LICENSE"
    "ThirdParty/fake-indexeddb/fake-indexeddb.js")

add_library(IndexedDB ${SOURCES})
warnings_as_errors(IndexedDB)

target_include_directories(IndexedDB
    PUBLIC "Include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

target_link_libraries(IndexedDB
    PUBLIC JsRuntime
    PRIVATE Scheduling)

set_property(TARGET IndexedDB PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
