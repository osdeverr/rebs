macro(add_library _target)
_add_library(${_target} ${ARGN})
set_property(GLOBAL APPEND PROPERTY GlobalTargetList ${_target})
endmacro()

macro(add_executable _target)
_add_executable(${_target} ${ARGN})
set_property(GLOBAL APPEND PROPERTY GlobalTargetList ${_target})
endmacro()

macro(add_custom_target _target)
_add_custom_target(${_target} ${ARGN})
set_property(GLOBAL APPEND PROPERTY GlobalTargetList ${_target})
endmacro()

if (RE_CUSTOM_LINK_OPTS)
    add_link_options(${RE_CUSTOM_LINK_OPTS})
endif()

add_subdirectory(${RE_ORIGINAL_CMAKE_DIR} ${RE_BIN_OUT_DIR})

get_property(_allTargets GLOBAL PROPERTY GlobalTargetList)
message(STATUS "GlobalTargetList: ${_allTargets}")

set(RE_ADAPTED_META "")

string(APPEND RE_ADAPTED_META "project-name: ${PROJECT_NAME}\n")

string(APPEND RE_ADAPTED_META "targets:\n")

foreach(target ${_allTargets})
    string(APPEND RE_ADAPTED_META "  ${target}:\n")

    get_target_property(TGT_TYPE ${target} TYPE)
    string(APPEND RE_ADAPTED_META "      cmake-type: ${TGT_TYPE}\n")   

    if (${TGT_TYPE} STREQUAL "STATIC_LIBRARY" OR ${TGT_TYPE} STREQUAL "SHARED_LIBRARY" OR ${TGT_TYPE} STREQUAL "EXECUTABLE")
        string(APPEND RE_ADAPTED_META "      location: $<TARGET_FILE:${target}>\n")
    endif()
    
    get_target_property(TGT_BINARY_DIR ${target} BINARY_DIR)
    string(APPEND RE_ADAPTED_META "      binary-dir: ${TGT_BINARY_DIR}\n")

    get_target_property(TGT_INCLUDE_DIRS ${target} INCLUDE_DIRECTORIES)

    if (TGT_INCLUDE_DIRS)
        string(APPEND RE_ADAPTED_META "      include-dirs: \n")    
        foreach(dir ${TGT_INCLUDE_DIRS})
            string(APPEND RE_ADAPTED_META "          - ${dir}\n")
        endforeach() 
    endif()
    
    get_target_property(TGT_INCLUDE_DIRS ${target} INTERFACE_INCLUDE_DIRECTORIES)

    if (TGT_INCLUDE_DIRS)
        string(APPEND RE_ADAPTED_META "      include-dirs: \n")    
        foreach(dir ${TGT_INCLUDE_DIRS})
            string(APPEND RE_ADAPTED_META "          - ${dir}\n")
        endforeach() 
    endif()
    
    get_target_property(TGT_DEPS ${target} LINK_LIBRARIES)

    if (TGT_DEPS)
        string(APPEND RE_ADAPTED_META "      cmake-deps: \n")    
        foreach(dep ${TGT_DEPS})
            string(APPEND RE_ADAPTED_META "          - ${dep}\n")
        endforeach() 
    endif()
endforeach()

if (RE_ADAPTED_META AND RE_ADAPTED_META_FILE)
    file(GENERATE OUTPUT ${RE_ADAPTED_META_FILE} CONTENT ${RE_ADAPTED_META})
endif()

#
# USAGE:
# cmake -G Ninja -DRE_ORIGINAL_CMAKE_DIR=<src> -DRE_BIN_OUT_DIR=<out> -DCMAKE_BUILD_TYPE=<type> -B . <rebs>\data\cmake-adapter
#
