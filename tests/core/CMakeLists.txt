add_executable(core_test core_test.cpp)

target_link_libraries(core_test PRIVATE finkit::core GTest::gtest_main)

gtest_discover_tests(core_test)
