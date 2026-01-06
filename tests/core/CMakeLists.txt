add_executable(core_test
    core_test.cpp
)

target_link_libraries(core_test
    PRIVATE
        finkit::core
        Catch2::Catch2WithMain
)

catch_discover_tests(core_test)
