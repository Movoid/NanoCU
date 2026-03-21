file(GLOB_RECURSE mpmc_queue_sources mpmc_queue/*.cpp)

add_library(mpmc_queue SHARED ${mpmc_queue_sources})

target_include_directories(mpmc_queue PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(mpmc_queue PRIVATE mpmc_queue/)

target_link_libraries(mpmc_queue PUBLIC hazard_ptr)
