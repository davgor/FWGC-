// Public API: a sibling component includes fwg/api/ headers and nothing else from fwg/.
// This one forwards the header(s) below. The functions a sibling may call are the lines
// of generator/api.txt that name `ages`; tools/check_layers holds both rules.
#pragma once

#include "fwg/ages.hpp"
