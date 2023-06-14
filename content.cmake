# fetch necessary modules

include(FetchContent)

# CMakeExtensions
include(FetchContent)
message(STATUS "Fetching CMakeExtensions")
FetchContent_Declare(cmake-extensions
    GIT_REPOSITORY https://github.com/BabylonJS/CMakeExtensions.git
    GIT_TAG 7bdd0664181ce3d90dc94474464cea9b25b22db7)
FetchContent_MakeAvailable(cmake-extensions)
message(STATUS "Fetching CMakeExtensions - done")

if(IOS)
    FetchContent_Declare(ios-cmake
        GIT_REPOSITORY https://github.com/leetal/ios-cmake.git
        GIT_TAG 04d91f6675dabb3c97df346a32f6184b0a7ef845)

    message(STATUS "Fetching ios-cmake")
    FetchContent_MakeAvailable(ios-cmake)
    message(STATUS "Fetching ios-cmake - done")

    set(CMAKE_TOOLCHAIN_FILE "${ios-cmake_SOURCE_DIR}/ios.toolchain.cmake" CACHE PATH "")
    set(PLATFORM "OS64COMBINED" CACHE STRING "")
    set(DEPLOYMENT_TARGET "12" CACHE STRING "")
endif()
