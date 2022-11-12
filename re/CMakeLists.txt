set(RE_TARGET re)

include(../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_executable (${RE_TARGET} ${SRCS})

target_link_libraries(${RE_TARGET} PRIVATE re::buildkit)
target_link_libraries(${RE_TARGET} PRIVATE fmt::fmt)
target_link_libraries(${RE_TARGET} PRIVATE reproc++)
add_dependencies(${RE_TARGET} ninja)

set_target_properties(${RE_TARGET} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${RE_TARGET} PUBLIC .)
target_compile_definitions(${RE_TARGET} PRIVATE RE_TEST_EXAMPLES_FOLDER="${PROJECT_SOURCE_DIR}/examples")

add_custom_command(TARGET ${RE_TARGET} 
                   POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ninja> ${CMAKE_CURRENT_BINARY_DIR})
