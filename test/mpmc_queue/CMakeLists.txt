include(GoogleTest)

function(add_mpmc_queue_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE GTest::gtest_main)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  gtest_discover_tests(${target})
endfunction()

add_mpmc_queue_test(mpmc_queue_normap_test normal_test.cpp)
add_mpmc_queue_test(mpmc_queue_concurrent_test concurrent_test.cpp)
