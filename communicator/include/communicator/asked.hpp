// The question in a reply worth an answer (contract version 21): what the game hands the director with
// the reply's words (Director::words, fwgc_words), so a question the stranger never answers is kept and
// asked again once, at a later greeting.
#pragma once

#include <string>
#include <string_view>

#include "communicator/meaning.hpp"

namespace communicator {

// The last question of `reply` (a sentence ending in "?"), word for word as the reply says it, when it
// asks a fact of the stranger that stays true and would be worth asking again (their name, where they
// come from, what brought them, who someone was to them), as `meaning` hears it; empty for a reply with
// no question, and for a question about now or none at all ("Are you well?", "What'll it be?", "Is that
// so?"). Throws as Meaning::subject_of() does.
std::string question_to_keep(std::string_view reply, Meaning& meaning);

}  // namespace communicator
