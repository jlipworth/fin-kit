add_library(finkit_types)

target_sources(finkit_types PUBLIC FILE_SET CXX_MODULES FILES types.cppm)

target_compile_features(finkit_types PUBLIC cxx_std_20)

target_link_libraries(finkit_types PUBLIC QuantLib::QuantLib)

add_library(finkit::types ALIAS finkit_types)
