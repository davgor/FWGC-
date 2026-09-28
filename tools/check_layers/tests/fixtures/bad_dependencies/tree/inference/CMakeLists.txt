# FetchContent_Declare(named in a comment) is not a fetch
fwgc_dependency(good_src)
fwgc_dependency(missing_src)
FetchContent_Declare(other URL https://example.com/other.zip)
file(DOWNLOAD https://example.com/y.zip y.zip)
ExternalProject_Add(z URL https://example.com/z.zip)
fetchcontent_declare(lower URL https://example.com/lower.zip)
file(READ x y)
FetchContent_MakeAvailable(good_src)
