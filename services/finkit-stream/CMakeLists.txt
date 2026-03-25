add_executable(finkit-stream main.cpp)

target_link_libraries(finkit-stream PRIVATE finkit::core
                                            redis++::redis++_static)

target_compile_features(finkit-stream PRIVATE cxx_std_20)
