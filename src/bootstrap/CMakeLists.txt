add_library(finkit_bootstrap)

target_sources(finkit_bootstrap PUBLIC FILE_SET CXX_MODULES FILES
                                       bootstrap.cppm)

target_link_libraries(finkit_bootstrap PUBLIC finkit::types QuantLib::QuantLib
                                              Boost::boost spdlog::spdlog)

target_compile_definitions(finkit_bootstrap PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_bootstrap PUBLIC cxx_std_20)

add_library(finkit::bootstrap ALIAS finkit_bootstrap)
