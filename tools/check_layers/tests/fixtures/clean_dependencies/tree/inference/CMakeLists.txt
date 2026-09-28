# A build fetching through the manifest's helper, a name at a time.
include(${CMAKE_CURRENT_LIST_DIR}/../dependencies.cmake)
fwgc_dependency(engine SYSTEM)
FetchContent_MakeAvailable(engine)
file(READ ${CMAKE_CURRENT_LIST_DIR}/version.txt version)
