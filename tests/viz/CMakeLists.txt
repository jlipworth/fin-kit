add_executable(viz_test viz_test.cpp)

target_link_libraries(viz_test PRIVATE finkit::viz GTest::gtest_main)

gtest_discover_tests(viz_test)
