add_library(finkit_curves)

target_sources(finkit_curves PUBLIC FILE_SET CXX_MODULES FILES curves.cppm)

target_link_libraries(finkit_curves PUBLIC finkit::data QuantLib::QuantLib
                                           Boost::boost spdlog::spdlog)

target_compile_definitions(finkit_curves PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_curves PUBLIC cxx_std_20)

add_library(finkit::curves ALIAS finkit_curves)
