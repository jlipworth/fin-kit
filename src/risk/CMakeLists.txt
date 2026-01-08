add_library(finkit_risk)

target_sources(finkit_risk PUBLIC FILE_SET CXX_MODULES FILES risk.cppm
                                  risk-types.cppm risk-engine.cppm)

target_link_libraries(finkit_risk PUBLIC finkit::types finkit::trading
                                         spdlog::spdlog)

target_compile_definitions(finkit_risk PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_risk PUBLIC cxx_std_20)

add_library(finkit::risk ALIAS finkit_risk)
