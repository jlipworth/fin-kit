add_executable(finkit-stream main.cpp)

target_link_libraries(
  finkit-stream PRIVATE finkit::core finkit::data finkit::analysis
                        redis++::redis++_static)

target_compile_features(finkit-stream PRIVATE cxx_std_20)
target_compile_definitions(finkit-stream PRIVATE SPDLOG_FMT_EXTERNAL)
