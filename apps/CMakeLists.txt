add_executable(fwg_cli main.cpp)
set_target_properties(fwg_cli PROPERTIES OUTPUT_NAME fwg)
target_link_libraries(fwg_cli PRIVATE fwg fwg_flags)




# A smoke sweep across seeds and sizes. Not part of the test suite: it is slow,
# and it answers a broader question than a test should.
add_executable(fwg_sweep sweep.cpp)
target_link_libraries(fwg_sweep PRIVATE fwg fwg_flags)


