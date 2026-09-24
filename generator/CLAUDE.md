@AGENTS.md

# For Claude, in generator/

- **Delete the exes before a scratch build.** Delete `build/tests/Release/fwg_tests.exe` and
  `build/src/Release/fwg.lib` first. Under `%TEMP%`, even a successful incremental build can skip
  the relink (MSB8029), and a test then passes on a stale exe.
- **Grep a build log for both `error` and `warning`.** `fatal error LNK` escapes a `: error` grep.
- **Build scratch copies at a short path.** For example `%TEMP%\fwgb\a`. MSBuild's try-compile
  under a long base passes Windows' 260-character limit at configure time.
- **The test runner filters by TEST-name substring, not by file.**
- **A world-shape change is proved with document hashes, not reasoning.** Build before and after,
  generate the same seed and size, and compare SHA-256.
