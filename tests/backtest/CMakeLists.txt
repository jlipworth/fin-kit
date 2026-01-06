add_executable(backtest_test backtest_test.cpp)

target_link_libraries(backtest_test PRIVATE finkit::backtest GTest::gtest_main)

gtest_discover_tests(backtest_test)
