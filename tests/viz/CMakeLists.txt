add_executable(viz_test
    viz_test.cpp
)

target_link_libraries(viz_test
    PRIVATE
        finkit::viz
        Catch2::Catch2WithMain
)

catch_discover_tests(viz_test)
