add_executable(backtest_test
    backtest_test.cpp
)

target_link_libraries(backtest_test
    PRIVATE
        finkit::backtest
        Catch2::Catch2WithMain
)

catch_discover_tests(backtest_test)
