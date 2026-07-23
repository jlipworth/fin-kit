add_executable(trading_test trading_test.cpp)

target_link_libraries(trading_test PRIVATE finkit::trading GTest::gtest_main)

gtest_discover_tests(trading_test)
