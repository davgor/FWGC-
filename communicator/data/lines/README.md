# Written lines

The lines speakers answer in, read by `communicator/include/communicator/authored.hpp`
(`Lines::load` reads every `.txt` here, in name order). The communicator README's "Written
lines" says how they are chosen.

## Format

```
voice | situation | line
```

- **voice:**
  - a hero's archetype and alignment, as the director's `voice_key` gives it (`warden/LN`);
  - `post:<post>` for a roster post;
  - `any` for everyone.

  A speaker answers from their own bank first, then their archetype's `NN` bank when their
  alignment has none, then their post's, then `any`.
- **situation:** one of `Lines::situations()`, each with its blanks:

  | Situation | Blanks |
  |---|---|
  | `about_self` | |
  | `about_person` | `{name}` `{role}` `{where}` |
  | `about_place` | `{about}` |
  | `surroundings` | `{here}` `{nearest}` `{others}` `{danger}` |
  | `work_own` | `{own}` `{others}` |
  | `work_others` | `{others}` |
  | `work_none` | |
  | `unknown` | |
  | `refused_give` | `{giver}` |
  | `refused_release` | `{giver}` |
  | `refused_gone` | |
  | `refused` | `{reason}` |
  | `accepted`, `declined`, `abandoned`, `parted`, `not_understood` | |

  Every situation may also use `{speaker}`, `{post}` and `{place}`.
- **line:** a line with a blank its packet cannot fill is not said.
- **`[optional parts]`:** a part in brackets is left out whole when the packet cannot fill a
  blank in it, so one line serves a packet with a nest and one without. Put the space before a
  part inside its brackets: `The nearest is {nearest}.[ Mind the {danger}.]`
- **The fullest lines are said.** Of the lines that fit, only those that say the most of the
  packet are said. Write lines for what a full packet holds, and shorter ones for packets that
  hold less.

## Rules a test holds every row to

`every_line_is_spoken_and_keeps_the_voices_rules` tries each row alone against every test
packet. `communicator_eval --check-lines PATH` runs the same check on a file or a folder.
- A sentence starts with a word from `common_capitals()`, or with a blank. Not with `{post}` or
  `{role}`: the sentence's capital makes "castellan" read as a name the packet did not give.
- Past a sentence's start, a capital is "I" or a blank's.
- No barred word (`forbidden_words()`: compass points, money and pay), and no double quote.
- An outcome that is not an answer opens with its outcome's words (`reply_shape.cpp`) and is one
  sentence; `accepted` may run to two. A refusal's last sentence ("Speak to ...") is added by
  C++.
- An answer runs to three sentences at most.
- Some test packet speaks the row.

## Rules no test can hold

- **A line says only what its blanks hold.** It carries manner, never a fact of its own: no
  place, creature, duty, rule, time or distance outside a blank.
- **Draw a voice from a card's public part** (manner, voice, stance, sample line, never), never
  from what the card keeps back (belief, wants, fears, tells, lies). A stranger never hears those.
- **Never name the archetype or a title.** `{post}` says what the speaker does.
