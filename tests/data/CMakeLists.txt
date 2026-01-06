add_executable(data_test data_test.cpp)

target_link_libraries(data_test PRIVATE finkit::data GTest::gtest_main)

gtest_discover_tests(data_test)
