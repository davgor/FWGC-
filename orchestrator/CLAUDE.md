@AGENTS.md

# For Claude, in orchestrator/

- **Build at a short path.** For example
  `cmake -S orchestrator -B %TEMP%\fwgo -G "Visual Studio 17 2022" -A x64`: the generator's
  try-compile under a long base passes Windows' 260-character limit. Delete the test exe before a
  rebuild you mean to test, so a skipped relink cannot run the old one.
- **Keep packets alive while you read them.** A `Fact*` taken from a temporary `FactPacket`
  (`of_kind(d.respond(...))`) dangles at the end of the statement. That includes
  `for (auto* f : of_kind(d.respond(...), ...))`, whose packet C++20 destroys before the loop body
  runs. Copy what you need inside that one statement, or name the packet first. It bit twice
  while this component was built: an empty quest id, then a garbage map key.
