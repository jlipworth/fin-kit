add_library(finkit_trading)

target_sources(finkit_trading PUBLIC FILE_SET CXX_MODULES FILES trading.cppm
                                     trading-types.cppm trading-engine.cppm)

target_link_libraries(finkit_trading PUBLIC finkit::types spdlog::spdlog)

target_compile_definitions(finkit_trading PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_trading PUBLIC cxx_std_20)

add_library(finkit::trading ALIAS finkit_trading)
