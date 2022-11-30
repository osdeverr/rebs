cmake_minimum_required (VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project ("re" CXX)

if(WIN32)
	set(CMAKE_INSTALL_PREFIX "D:/Programs/ReBS")
endif()

set(NINJA_BUILD_BINARY ON)
set(BUILD_TESTING OFF)
add_subdirectory(3rdparty/ninja)

add_subdirectory(3rdparty/ghc-filesystem)
add_subdirectory(3rdparty/yaml-cpp)
add_subdirectory(3rdparty/magic-enum)
add_subdirectory(3rdparty/fmt)

set(REPROC++ ON)
add_subdirectory(3rdparty/reproc)

add_subdirectory(rebuildkit)
add_subdirectory(reutils)

#add_subdirectory(re-tool-build)
#add_subdirectory(re-tool-install)
#add_subdirectory(re-tool-new)
#add_subdirectory(re-tool-run)

add_subdirectory(re)
