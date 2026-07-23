add_executable(var_test var_test.cpp)

target_link_libraries(var_test PRIVATE finkit::risk finkit::stats GTest::gtest_main)

gtest_discover_tests(var_test)

add_executable(stress_test stress_test.cpp)

target_link_libraries(stress_test PRIVATE finkit::risk GTest::gtest_main)

gtest_discover_tests(stress_test)

add_executable(risk_test risk_test.cpp)

target_link_libraries(risk_test PRIVATE finkit::risk GTest::gtest_main)

gtest_discover_tests(risk_test)
