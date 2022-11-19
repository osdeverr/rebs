set(RE_TARGET re-tool-run)

include(../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_executable (${RE_TARGET} ${SRCS})

target_link_libraries(${RE_TARGET} PRIVATE re::buildkit)
target_link_libraries(${RE_TARGET} PRIVATE fmt::fmt)
target_link_libraries(${RE_TARGET} PRIVATE reproc++)

set_target_properties(${RE_TARGET} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${RE_TARGET} PUBLIC .)

install(TARGETS ${RE_TARGET}
        DESTINATION bin)
