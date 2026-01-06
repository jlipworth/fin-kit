add_executable(analysis_test
    analysis_test.cpp
)

target_link_libraries(analysis_test
    PRIVATE
        finkit::analysis
        Catch2::Catch2WithMain
)

catch_discover_tests(analysis_test)
