set(RE_TARGET reutils)

include(../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_library (${RE_TARGET} STATIC ${SRCS})

set_target_properties(${RE_TARGET} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${RE_TARGET} PUBLIC .)

add_library(re::utils ALIAS ${RE_TARGET})
