set(CMW_ProjectName yaml-cpp)

include(../../find_source_files.cmake)
find_source_files_curr_dir(SRCS)

add_library (${CMW_ProjectName} STATIC ${SRCS})
set_target_properties(${CMW_ProjectName} PROPERTIES LINKER_LANGUAGE CXX)

target_compile_definitions(${CMW_ProjectName} PUBLIC YAML_CPP_STATIC_DEFINE)

target_include_directories(${CMW_ProjectName} PUBLIC include)
