@AGENTS.md

# For Claude

- **Do not certify your own work.** A finding you just decided your fresh code "already handles" is
  the least reliable verdict there is. Have a separate refuter check it against the file. The
  commit gate's red team is an independent check; answer its findings, do not argue past them.
- **`ctest --no-tests=error`, always.** Without it, a filter that selects nothing reads as a pass.
- **Committing from the Bash tool.** `git commit` runs a review. Give the command a 600000 ms
  timeout, or run `commit_gate review -m "<message>"` in the background first, then commit with
  the same message.
- **Write code files with the Write tool, not heredocs.** An escaped `\n` or `\\` written through a
  heredoc arrives changed.
- **Files change underneath you.** Other agents edit this tree at the same time. Check modification
  times before calling a failure yours, and never undo with checkout, restore, reset or stash.
