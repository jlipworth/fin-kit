add_library(finkit_valuation)

target_sources(
  finkit_valuation PUBLIC FILE_SET CXX_MODULES FILES valuation.cppm
                          valuation-bond.cppm valuation-swap.cppm)

target_link_libraries(
  finkit_valuation PUBLIC finkit::types QuantLib::QuantLib Boost::boost
                          spdlog::spdlog Eigen3::Eigen)

target_compile_definitions(finkit_valuation PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_valuation PUBLIC cxx_std_20)

add_library(finkit::valuation ALIAS finkit_valuation)
