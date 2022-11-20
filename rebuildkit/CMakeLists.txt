set(RE_TARGET rebuildkit)

include(../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_library (${RE_TARGET} STATIC ${SRCS})

target_link_libraries(${RE_TARGET} PRIVATE ghc_filesystem)
target_link_libraries(${RE_TARGET} PUBLIC yaml-cpp)
target_link_libraries(${RE_TARGET} PUBLIC re::utils)
target_link_libraries(${RE_TARGET} PUBLIC magic_enum)
target_link_libraries(${RE_TARGET} PRIVATE fmt::fmt)
target_link_libraries(${RE_TARGET} PUBLIC reproc++)

set_target_properties(${RE_TARGET} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${RE_TARGET} PUBLIC .)

add_library(re::buildkit ALIAS ${RE_TARGET})
