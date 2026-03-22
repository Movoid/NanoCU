file(GLOB_RECURSE hazard_ptr_sources hazard_ptr/*.h)
add_library(hazard_ptr INTERFACE ${hazard_ptr_sources})
target_include_directories(hazard_ptr INTERFACE ${PROJECT_SOURCE_DIR}/lib)

file(GLOB_RECURSE nano_utils_sources nano_utils/*.h)
add_library(nano_utils INTERFACE ${utility_source})
target_include_directories(nano_utils INTERFACE ${PROJECT_SOURCE_DIR}/lib)
