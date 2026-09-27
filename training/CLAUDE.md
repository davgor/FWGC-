@AGENTS.md

# For Claude, in training/

- **Start a long run detached.** A training run started from the Bash tool died with the app
  (2026-09-27). Start it with PowerShell's `Start-Process -WindowStyle Hidden`, logging to a file,
  and watch the log.
- **The converter needs `sentencepiece` installed to reach Qwen's BPE vocabulary:** it imports the
  module before it looks for `tokenizer.model`, so without it the conversion stops.
- **Build the llama.cpp tools at a short path** (`%TEMP%\lq`), from the same pinned source the
  inference build fetches, with `LLAMA_OPENSSL=OFF`: the quantizer needs no network.
