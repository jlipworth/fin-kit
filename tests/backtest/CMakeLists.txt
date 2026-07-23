add_executable(backtest_test backtest_test.cpp)

target_link_libraries(backtest_test PRIVATE finkit::backtest GTest::gtest_main)

gtest_discover_tests(backtest_test)

add_executable(realism_test realism_test.cpp)

target_link_libraries(realism_test PRIVATE finkit::backtest GTest::gtest_main)

gtest_discover_tests(realism_test)

add_executable(integration_test integration_test.cpp)

target_link_libraries(integration_test PRIVATE finkit::backtest GTest::gtest_main)

gtest_discover_tests(integration_test)
