@AGENTS.md

# For Claude, in communicator/

- **Build at a short path,** such as `%TEMP%\fwgc`. The build compiles llama.cpp through
  `../inference`, whose paths pass Windows' 260-character limit under a long base.
- **Check a control against the model, not memory.** Told to copy "the burnt works", the 0.5B
  test model wrote "the burning works", and that control proved nothing until it was changed.
- **Prompt changes move the small model's answers around.** While this component was built,
  each rewording fixed some lines and broke others. Change a prompt for a reason that holds for
  any model, re-measure over many lines, and report the measure. Never tune to one test.
- **Mutate a scratch copy of the tree,** never the shared one. The component needs `generator/`,
  `orchestrator/` and `inference/` beside it to configure.
