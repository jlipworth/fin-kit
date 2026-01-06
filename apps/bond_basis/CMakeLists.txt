add_executable(bond_basis main.cpp)

target_link_libraries(bond_basis PRIVATE finkit::analysis finkit::data)
