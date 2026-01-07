add_library(finkit_backtest)

target_sources(finkit_backtest PUBLIC FILE_SET CXX_MODULES FILES backtest.cppm)

target_link_libraries(finkit_backtest PUBLIC finkit::analysis)

target_compile_features(finkit_backtest PUBLIC cxx_std_20)

add_library(finkit::backtest ALIAS finkit_backtest)
