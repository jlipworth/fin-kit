add_library(finkit_backtest)

target_sources(
  finkit_backtest
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         backtest.cppm
         backtest-types.cppm
         backtest-strategy.cppm
         backtest-engine.cppm)

target_link_libraries(
  finkit_backtest PUBLIC finkit::types finkit::trading finkit::risk
                         QuantLib::QuantLib uuid)

target_compile_features(finkit_backtest PUBLIC cxx_std_20)

add_library(finkit::backtest ALIAS finkit_backtest)
