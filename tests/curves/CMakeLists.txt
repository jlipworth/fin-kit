add_executable(curves_test curves_test.cpp)

target_link_libraries(curves_test PRIVATE finkit::curves GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(curves_test)
