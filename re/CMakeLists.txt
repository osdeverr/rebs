set(RE_TARGET re)

include(../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_executable (${RE_TARGET} ${SRCS})

target_link_libraries(${RE_TARGET} PRIVATE re::buildkit)
target_link_libraries(${RE_TARGET} PRIVATE fmt::fmt)
target_link_libraries(${RE_TARGET} PRIVATE reproc++)

add_dependencies(${RE_TARGET} ninja)

add_custom_command(TARGET ${RE_TARGET} 
                   POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ninja> ${CMAKE_CURRENT_BINARY_DIR})
                   
add_custom_command(TARGET ${RE_TARGET} 
                   POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory  ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_CURRENT_BINARY_DIR}/data)
                   
#add_custom_command(TARGET ${RE_TARGET} 
#                   POST_BUILD
#                   COMMAND ${CMAKE_COMMAND} -E copy_directory  ${CMAKE_CURRENT_SOURCE_DIR}/root ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS ${RE_TARGET}
        DESTINATION bin)
install(FILES $<TARGET_FILE:ninja> DESTINATION bin)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION bin)

#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/root/re.cmd DESTINATION bin)
#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/root/re.ps1 DESTINATION bin)

#install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/root/re-tool-env.cmd DESTINATION bin)
