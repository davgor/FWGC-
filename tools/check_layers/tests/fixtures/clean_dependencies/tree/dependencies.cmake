# The fetch helper layers.txt names; FetchContent_Declare lives here and nowhere else.
function(fwgc_dependency name)
  FetchContent_Declare(${name} URL "cached" URL_HASH SHA256=0)
endfunction()
