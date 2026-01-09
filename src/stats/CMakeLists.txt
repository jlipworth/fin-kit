add_library(finkit_stats)

target_sources(
  finkit_stats
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         stats.cppm
         stats-rolling.cppm
         stats-covariance.cppm
         stats-signals.cppm
         stats-returns.cppm)

target_link_libraries(finkit_stats PUBLIC spdlog::spdlog Eigen3::Eigen)

target_compile_definitions(finkit_stats PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_stats PUBLIC cxx_std_20)

add_library(finkit::stats ALIAS finkit_stats)
