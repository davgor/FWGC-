// Public API: a sibling component includes fwg/api/ headers and nothing else from fwg/.
// This one forwards the header(s) below. The functions a sibling may call are the lines
// of generator/api.txt that name `person_state`; tools/check_layers holds both rules.
#pragma once

#include "fwg/person_state.hpp"
