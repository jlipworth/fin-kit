add_executable(bootstrap_test bootstrap_test.cpp)

target_link_libraries(bootstrap_test PRIVATE finkit::bootstrap
                                             GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(bootstrap_test)
