add_executable(data_test
    data_test.cpp
)

target_link_libraries(data_test
    PRIVATE
        finkit::data
        Catch2::Catch2WithMain
)

catch_discover_tests(data_test)
