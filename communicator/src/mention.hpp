// A name the player says outright needs no guessing.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "communicator/known.hpp"

namespace communicator {

// The one thing of `kind` that `line` names outright: its label's words appear in the line, in
// order and whole, ignoring case, typographic punctuation and any part of the label in brackets
// ("Hakndum" names "Hakndum (Age 2)"). When several are named, the one with the longest name
// ("the hamlet of Wenweg" over "Wenweg"); nullopt when none is, or when two tie.
std::optional<Referent> named_outright(Kind kind, const Known& known, std::string_view line);

// The name `line` gives as the player's own (contract version 24), in their words: after "my name
// is", "call me", "I go by", "they know me as"; after "I'm" or "I am", a word written with a capital,
// or in a line written all in lower case, the last word; before "is my name" or "'s the name"; or
// alone, when `last_reply` asked for it ("And you are?"). A run of words written with capitals, "of"
// and "the" between them ("Sir Aldric of the Vale"), or in lower case up to three words; a leading
// "the" or "a" dropped, each word's first letter raised ("brenna" is "Brenna"), at most 60 bytes.
// nullopt when the line gives none: "I'm tired", "call me a fool", "don't call me that", "I'm
// Selbren's man", "Nobody important".
std::optional<std::string> name_given(std::string_view line, std::string_view last_reply = {});

}  // namespace communicator
