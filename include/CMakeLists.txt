file(GLOB_RECURSE mpmc_queue_sources mpmc_queue/*.h)
add_library(mpmc_queue INTERFACE ${mpmc_queue_sources})
target_link_libraries(mpmc_queue INTERFACE hazard_ptr)

file(GLOB_RECURSE lockfree_mpmc_queue_sources lockfree_mpmc_queue/*.h)
add_library(lockfree_mpmc_queue INTERFACE ${lockfree_mpmc_queue_sources})
target_link_libraries(lockfree_mpmc_queue INTERFACE hazard_ptr)
