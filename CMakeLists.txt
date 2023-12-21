cmake_minimum_required (VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(vcpkg
    GIT_REPOSITORY https://github.com/microsoft/vcpkg/
    GIT_TAG 2023.12.12
)
FetchContent_MakeAvailable(vcpkg)

# NOTE: This must be defined before the first project call
set(CMAKE_TOOLCHAIN_FILE "${vcpkg_SOURCE_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")

project ("re" CXX)

if(WIN32)
	set(CMAKE_INSTALL_PREFIX "D:/Programs/ReBS")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_compile_definitions(_LIBCPP_DISABLE_AVAILABILITY)
endif()

set(NINJA_BUILD_BINARY ON)
set(BUILD_TESTING OFF)
add_subdirectory(3rdparty/ninja)

add_subdirectory(3rdparty/ghc-filesystem)
add_subdirectory(3rdparty/yaml-cpp)
add_subdirectory(3rdparty/magic-enum)
add_subdirectory(3rdparty/fmt)
add_subdirectory(3rdparty/nlohmann-json)

set(REPROC++ ON)
add_subdirectory(3rdparty/reproc)

add_subdirectory(rebuildkit)
add_subdirectory(reutils)

#add_subdirectory(re-tool-build)
#add_subdirectory(re-tool-install)
#add_subdirectory(re-tool-new)
#add_subdirectory(re-tool-run)

add_subdirectory(re)
