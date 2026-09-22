add_executable(fwg_cli main.cpp)
set_target_properties(fwg_cli PROPERTIES OUTPUT_NAME fwg)
target_link_libraries(fwg_cli PRIVATE fwg fwg_flags)


