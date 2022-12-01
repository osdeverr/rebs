set(RE_TARGET rebuildkit)

include(../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_library (${RE_TARGET} STATIC ${SRCS})

find_package(Boost REQUIRED)

target_link_libraries(${RE_TARGET} PUBLIC ghc_filesystem)
target_link_libraries(${RE_TARGET} PUBLIC yaml-cpp)
target_link_libraries(${RE_TARGET} PUBLIC re::utils)
target_link_libraries(${RE_TARGET} PUBLIC magic_enum)
target_link_libraries(${RE_TARGET} PRIVATE fmt::fmt)
target_link_libraries(${RE_TARGET} PUBLIC reproc++)
target_link_libraries(${RE_TARGET} PUBLIC reproc++)
target_link_libraries(${RE_TARGET} PUBLIC ${Boost_LIBRARIES})

if(UNIX)
    target_link_libraries(${RE_TARGET} PUBLIC dl)
    target_compile_definitions(${RE_TARGET} PUBLIC BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)
endif()

set_target_properties(${RE_TARGET} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${RE_TARGET} PUBLIC .)
target_include_directories(${RE_TARGET} PUBLIC ../3rdparty/fmt/include)
target_include_directories(${RE_TARGET} PUBLIC ${Boost_INCLUDE_DIRS})

add_library(re::buildkit ALIAS ${RE_TARGET})
