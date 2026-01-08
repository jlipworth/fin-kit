add_library(finkit_basis)

target_sources(finkit_basis PUBLIC FILE_SET CXX_MODULES FILES basis.cppm)

target_link_libraries(finkit_basis PUBLIC finkit::types QuantLib::QuantLib
                                          Boost::boost spdlog::spdlog)

target_compile_definitions(finkit_basis PUBLIC SPDLOG_FMT_EXTERNAL)

target_compile_features(finkit_basis PUBLIC cxx_std_20)

add_library(finkit::basis ALIAS finkit_basis)
