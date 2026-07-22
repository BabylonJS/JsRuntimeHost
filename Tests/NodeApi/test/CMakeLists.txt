if(WIN32)
  # "npx" interrupts current shell script execution without the "call"
  set(npx cmd /c npx)
  set(npm cmd /c npm)
else()
  set(npx "npx")
  set(npm "npm")
endif()

# copy JS Tools package files
add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/babel.config.js
    ${CMAKE_CURRENT_BINARY_DIR}/package.json
    ${CMAKE_CURRENT_BINARY_DIR}/package-lock.json
    ${CMAKE_CURRENT_BINARY_DIR}/postinstall.mjs
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/babel.config.js
    ${CMAKE_CURRENT_SOURCE_DIR}/package.json
    ${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json
    ${CMAKE_CURRENT_SOURCE_DIR}/postinstall.mjs
    ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/babel.config.js
    ${CMAKE_CURRENT_SOURCE_DIR}/package.json
    ${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json
    ${CMAKE_CURRENT_SOURCE_DIR}/postinstall.mjs
)
add_custom_target(copyNodeApiJSToolsFiles)

# Install exactly the lockfile graph. postinstall.mjs writes the declared
# output from the lockfile itself, avoiding the old racy background tar/hash
# pipeline and making incremental rebuilds deterministic on every platform.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_modules.sha256
  DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/package.json
    ${CMAKE_CURRENT_BINARY_DIR}/package-lock.json
    ${CMAKE_CURRENT_BINARY_DIR}/postinstall.mjs
  COMMAND ${npm} ci
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_custom_target(installNodeApiTestJsTools
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/node_modules.sha256
)
add_dependencies(installNodeApiTestJsTools copyNodeApiJSToolsFiles)

# add the Babel transform commands for each test JS file
set(testJSRootDir ${CMAKE_CURRENT_SOURCE_DIR})

# Collect all .js files recursively
file(GLOB_RECURSE basicsTestJSFiles "basics/*.js")
file(GLOB_RECURSE commonTestJSFiles "common/*.js")
file(GLOB_RECURSE jsNativeApiTestJSFiles "js-native-api/*.js")
set(testJSFiles
  ${basicsTestJSFiles}
  ${commonTestJSFiles}
  ${jsNativeApiTestJSFiles})

foreach(absoluteTestJSFile ${testJSFiles})
  # create target directory
  file(RELATIVE_PATH testJSFile ${testJSRootDir} ${absoluteTestJSFile})
  get_filename_component(testJSDir ${testJSFile} DIRECTORY)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${testJSDir})

  # generate Hermes-compatible JavaScript code
  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}
      ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${testJSFile}
    COMMAND
      ${npx}
      "babel"
      "--retain-lines"
      "--source-maps"
      "true"
      "--out-file"
      "${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}"
      "--source-map-target"
      "${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map"
      "${CMAKE_CURRENT_SOURCE_DIR}/${testJSFile}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )

  # build a list of all outputs
  list(APPEND transformedJSFiles
    ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}
    ${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map
  )

  # node_api_copy_test_sources() first stages the complete source tree (native
  # module layout and non-JS fixtures included). Overlay only Babel's generated
  # JS and source map afterward, without copying node_modules into the runtime
  # directory. The second overlay is needed because NodeApiTests has its own
  # post-build source copy after node_lite has already been built.
  foreach(nodeApiRunner node_lite NodeApiTests)
    add_custom_command(TARGET ${nodeApiRunner} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${nodeApiRunner}>/test/${testJSDir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}"
        "$<TARGET_FILE_DIR:${nodeApiRunner}>/test/${testJSFile}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/${testJSFile}.map"
        "$<TARGET_FILE_DIR:${nodeApiRunner}>/test/${testJSFile}.map"
      VERBATIM)
  endforeach()
endforeach()

# run the Babel transforms for all required output files
add_custom_target(transformJSFiles
  DEPENDS ${transformedJSFiles}
)
add_dependencies(transformJSFiles installNodeApiTestJsTools)
add_dependencies(node_lite transformJSFiles)
add_dependencies(NodeApiTests transformJSFiles)

if(JSR_NODE_API_BUILD_NATIVE_TESTS)
  add_subdirectory(js-native-api)
endif()
