# training/: local rules

Dev-time fine-tuning of the NPC voice model on the written lines. **Lane: Claude.** A dev tool in
Python (ruled 2026-09-27: the pinned llama.cpp cannot fine-tune a 4B model; the owner chose a
Python LoRA used only to train). The repository-wide rules are in [`../AGENTS.md`](../AGENTS.md),
and [`README.md`](README.md) says how to run it and what it measured.

## Rules

- **Nothing here ships, and nothing that ships reads it.** The runtime stays C++ and llama.cpp. A
  trained model is copied into `inference/models/` (gitignored) under its own name; no build, test
  or runtime path points into `training/`.
- **Commit no weights, data or builds.** `.venv/`, `data/`, `out/` and every `*.gguf` are
  gitignored. The base weights live in `inference/models/Qwen3-4B-Instruct-2507-hf/`, gitignored
  like every model.
- **Every download needs the owner's approval by file, source and size**, and is checked against
  its published size and SHA-256 before use (approved 2026-09-27: the Qwen3-4B-Instruct-2507
  safetensors at revision cdbee75f, the torch 2.11.0+cu128 wheel, and the small PyPI packages in
  `requirements.txt`). Pin every version there.
- **A pair is exactly what the game's voice sends.** Prompts come from the communicator's own
  `Voice::messages` through `communicator_eval --export-pairs`, never rebuilt here, and replies
  are the written lines `Lines::speak` would say. The chat template must render the runtime's
  layout (plain ChatML for Qwen3); check it when the base model changes.
- **Only the reply is learned.** The prompt's tokens carry no loss.
- **Hold out a world.** The last world played is the validation set, never trained on.
- **Measure a trained model as any model change:** `communicator_eval` before and after at the
  same device and threads, the interpreter's lines and the voice's cases both (one model serves
  both), with a blind judge, and against the base quantized the same way, so the tuning and the
  quantization are not confused.
