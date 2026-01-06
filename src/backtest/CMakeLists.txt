add_library(finkit_backtest
    src/backtest.cpp
)

target_include_directories(finkit_backtest
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(finkit_backtest
    PUBLIC finkit::analysis
)

target_compile_features(finkit_backtest PUBLIC cxx_std_20)

add_library(finkit::backtest ALIAS finkit_backtest)
