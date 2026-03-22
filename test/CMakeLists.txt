function(add_general_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE GTest::gtest_main)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/lib)
  gtest_discover_tests(${target})
endfunction()

include(GoogleTest)

add_subdirectory(mpmc_queue)
add_subdirectory(nano_utils)
add_subdirectory(hazard_ptr)
