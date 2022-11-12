
function(group_files_by_directory)
	cmake_parse_arguments(GROUP_FILES "" "" "SRCS" ${ARGN})

	foreach(FILE ${GROUP_FILES_SRCS})
		# Get the directory of the source file
		get_filename_component(PARENT_DIR ${FILE} DIRECTORY)

        # Remove common directory prefix to make the group
        string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")

		# Make sure we are using windows slashes
		string(REPLACE "/" "\\" GROUP "${GROUP}")

		# Group into "Source Files" and "Header Files"
		#    if ("${FILE}" MATCHES ".*\\.cpp")
		#       set(GROUP "Source Files${GROUP}")
		#    elseif("${FILE}" MATCHES ".*\\.h")
		#       set(GROUP "Header Files${GROUP}")
		#    endif()

        if(NOT "${GROUP}" STREQUAL "")
		    source_group(${GROUP} FILES ${FILE})
        endif()
	endforeach()
endfunction()

function(find_arbitrary_source_files WILDCARDS SOURCES)
	file(GLOB_RECURSE TMP_SRC ${WILDCARDS})
	group_files_by_directory(SRCS ${TMP_SRC})
	set(${SOURCES} ${TMP_SRC} PARENT_SCOPE)
endfunction()

function(find_source_files PATH SOURCES)
	file(GLOB_RECURSE TMP_SRC ${PATH}/*.cpp ${PATH}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cc ${PATH}/*.hpp ${PATH}/*.h ${PATH}/*.asm)
	group_files_by_directory(SRCS ${TMP_SRC})
	set(${SOURCES} ${TMP_SRC} PARENT_SCOPE)
endfunction()

function(find_source_files_curr_dir SOURCES)
	file(GLOB_RECURSE TMP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.cc ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/*.asm)
	group_files_by_directory(SRCS ${TMP_SRC})
	set(${SOURCES} ${TMP_SRC} PARENT_SCOPE)
endfunction()
