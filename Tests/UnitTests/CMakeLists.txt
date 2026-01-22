set(STATIC_SCRIPT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/symlink_target.js")

set(GENERATED_SCRIPTS
    "${JSRUNTIMEHOST_OUTPUT_DIR}/tests.js")

set(TYPE_SCRIPTS
    "Scripts/tests.ts")

set(SOURCES
    "Shared/Shared.cpp"
    "CompatibilityTests.cpp"
    "Shared/Shared.h")

if(APPLE)
    if(IOS)
        set(SOURCES ${SOURCES}
            "iOS/App.mm")

        set(PLIST_FILE
            "${CMAKE_CURRENT_LIST_DIR}/iOS/Info.plist")

        set(STORYBOARD
            "${CMAKE_CURRENT_LIST_DIR}/iOS/Base.lproj/LaunchScreen.storyboard"
            "${CMAKE_CURRENT_LIST_DIR}/iOS/Base.lproj/Main.storyboard")

        set_source_files_properties(
            ${STATIC_SCRIPT_SOURCES}
            ${GENERATED_SCRIPTS}
            ${TYPE_SCRIPTS}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Scripts")
    else()
        set(SOURCES ${SOURCES}
            macOS/App.mm)
    endif()
elseif(WIN32)
    set(SOURCES ${SOURCES}
        Win32/App.cpp)
elseif(UNIX AND NOT ANDROID)
    set(SOURCES ${SOURCES}
        Linux/App.cpp)
endif()

add_executable(UnitTests ${SOURCES} ${STATIC_SCRIPT_SOURCES} ${GENERATED_SCRIPTS} ${TYPE_SCRIPTS})
target_compile_definitions(UnitTests PRIVATE JSRUNTIMEHOST_PLATFORM="${JSRUNTIMEHOST_PLATFORM}")

target_link_libraries(UnitTests
    PRIVATE AppRuntime
    PRIVATE Console
    PRIVATE AbortController
    PRIVATE Scheduling
    PRIVATE ScriptLoader
    PRIVATE URL
    PRIVATE UrlLib
    PRIVATE XMLHttpRequest
    PRIVATE WebSocket
    PRIVATE gtest_main
    PRIVATE Foundation
    PRIVATE Blob
    PRIVATE Performance
    ${ADDITIONAL_LIBRARIES})

# See https://gitlab.kitware.com/cmake/cmake/-/issues/23543
# If we can set minimum required to 3.26+, then we can use the `copy -t` syntax instead.
add_custom_command(TARGET UnitTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:UnitTests>>,copy,true> $<TARGET_RUNTIME_DLLS:UnitTests> $<TARGET_FILE_DIR:UnitTests> COMMAND_EXPAND_LISTS)

if(IOS)
    set_target_properties(UnitTests PROPERTIES
        MACOSX_BUNDLE true
        MACOSX_BUNDLE_INFO_PLIST "${PLIST_FILE}"
        RESOURCE "${STORYBOARD}"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.jsruntimehost.unittests")
else()
    foreach(SCRIPT ${STATIC_SCRIPT_SOURCES})
        get_filename_component(SCRIPT_NAME "${SCRIPT}" NAME)
        add_custom_command(
            OUTPUT "${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${SCRIPT}" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
            COMMENT "Copying ${SCRIPT_NAME}"
            MAIN_DEPENDENCY "${SCRIPT}")
    endforeach()

    foreach(SCRIPT ${GENERATED_SCRIPTS})
        get_filename_component(SCRIPT_NAME "${SCRIPT}" NAME)
        add_custom_command(
            OUTPUT "${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${SCRIPT}" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
            COMMENT "Copying ${SCRIPT_NAME}"
            MAIN_DEPENDENCY "${SCRIPT}")
    endforeach()

    add_custom_command(TARGET UnitTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/symlink_target.js" "${CMAKE_CFG_INTDIR}/Scripts/symlink_1.js")
    add_custom_command(TARGET UnitTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/symlink_1.js" "${CMAKE_CFG_INTDIR}/Scripts/symlink_2.js")
endif()

set_property(TARGET UnitTests PROPERTY FOLDER Tests)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${STATIC_SCRIPT_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Scripts PREFIX scripts FILES ${TYPE_SCRIPTS})
