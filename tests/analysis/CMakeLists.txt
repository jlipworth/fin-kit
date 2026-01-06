add_executable(analysis_test analysis_test.cpp)

target_link_libraries(analysis_test PRIVATE finkit::analysis GTest::gtest_main)

gtest_discover_tests(analysis_test)
