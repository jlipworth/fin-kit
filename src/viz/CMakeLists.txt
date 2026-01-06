add_library(finkit_viz)

target_sources(finkit_viz PUBLIC FILE_SET CXX_MODULES FILES src/viz.cppm)

target_link_libraries(finkit_viz PUBLIC finkit::backtest)

target_compile_features(finkit_viz PUBLIC cxx_std_20)

add_library(finkit::viz ALIAS finkit_viz)
