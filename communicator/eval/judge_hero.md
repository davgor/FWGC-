# The hero benchmark's judge

What a blind judge of `session_hero.txt` is given, word for word (README, "The hero benchmark"). Put the
transcript's path in place of TRANSCRIPT_PATH; hand it to a fresh reader each time, who has seen
no earlier round.

---

You are judging a transcript of dialogue from a fantasy game, blind: you do not know how the replies
were produced, and nothing is expected of you but a careful, honest reading. READ ONLY: read the one
file named below and nothing else; edit, create or delete nothing; run nothing; download nothing.

The transcript: TRANSCRIPT_PATH. Read all of it before judging anything.

The question it answers: can a player hold a real conversation with this character, a hero of the
world (larger than life, not a shopkeeper), across several visits? Not a menu of facts: a
conversation, where what the player says is heard and answered, the character stays themself, and
what passed before is remembered.

What the transcript shows:
- `== name: Person, post, at Place` starts a stretch of the player talking to one person.
- Each numbered turn is the player's line, then the reply, then in brackets what the game recorded:
  the act the player's line was read as, the outcome, and what the character remembers of the player.
- Lines between turns are things that happened: time passing, work finished, and "compacted": the
  character's memory of a conversation tidied into what they keep ("memory ..." lines). They should
  remember what those lines say, and nothing the player never said.

For every numbered reply, list its faults, or OK:
- SENSE: it does not follow from the player's line or the situation, or it is garbled.
- DODGE: it does not answer or engage with what the player actually said: a generic line where the
  player asked or told something specific (a question about the character answered with the weather,
  a story of hardship met with a shrug).
- MEMORY: it misremembers or forgets what the transcript shows the character knows.
- REPEAT: it repeats what the same character already said, without cause.
- INVENTS: it claims a fact the transcript gives no ground for.
- CHARACTER: it does not sound like the same person as their other replies, or not like a hero.
- TONE: its manner does not fit the moment.
A reply can have several faults. A reply with none is OK.

Then answer, separately:
1. The illusion: at which turn does it first stop feeling like a conversation with a person, and
   why? (Say "never" if it holds.)
2. Real-ish conversation, 1 to 5: 1 a vending machine, 3 a character with set answers that sometimes
   land, 5 a person you could keep talking to. One paragraph of reasons.
3. The three changes that would most improve it, most important first: concrete, about what the
   character would need to be able to say or understand, not about wording.

Output:
1. A table: turn | faults (or OK) | one-line reason.
2. Totals: replies OK, and the count of each fault.
3. The illusion, the score, and the three changes.
