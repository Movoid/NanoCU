file(GLOB_RECURSE hazard_ptr_sources hazard_ptr/*.cpp)

add_library(hazard_ptr STATIC ${hazard_ptr_sources})
