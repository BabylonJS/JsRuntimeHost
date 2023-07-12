if(NAPI_JAVASCRIPT_ENGINE STREQUAL "JSI")
    add_subdirectory(Node-API-JSI)
else()
    add_subdirectory(Node-API)
endif()

add_subdirectory(JsRuntime)

if(JSRUNTIMEHOST_CORE_APPRUNTIME)
    add_subdirectory(AppRuntime)
endif()

if(JSRUNTIMEHOST_CORE_SCRIPTLOADER)
    add_subdirectory(ScriptLoader)
endif()

add_on_linked_as_dependency_cmake_file(napi "${CMAKE_CURRENT_SOURCE_DIR}/OnLinkedAsDependency.cmake")