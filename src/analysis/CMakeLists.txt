add_library(finkit_analysis)

target_sources(finkit_analysis PUBLIC FILE_SET CXX_MODULES FILES analysis.cppm)

target_link_libraries(
  finkit_analysis PUBLIC finkit::types finkit::data QuantLib::QuantLib
                         Boost::boost spdlog::spdlog)

# SPDLOG_FMT_EXTERNAL needed for module scanning (Conan provides it for compile
# but not scan phase)
target_compile_definitions(finkit_analysis PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_analysis PUBLIC cxx_std_20)

add_library(finkit::analysis ALIAS finkit_analysis)
