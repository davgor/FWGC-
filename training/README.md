# training/

Dev-time fine-tuning of the NPC voice (Qwen3-4B-Instruct-2507) on the written lines, so the
model speaks as the lines do: in the speaker's voice, saying only what the packet holds. The
owner, 2026-09-27: "Lets get to it then I'd like this up to speed", after a blind judge found every
model tried inventing scenery and distances that no written line invents. The pinned llama.cpp
cannot fine-tune a 4B model (FP32 full training only; its README says a 1B model already needs
about 24 GB), so the owner chose a LoRA trained in Python, for development only. Nothing here
ships: the runtime loads the result as a GGUF through llama.cpp, like any model.

## The pipeline

1. **Pairs** (`make_pairs.py`). For 15 worlds (seeds 101 to 115, size 33, 30 days on), 50 speakers
   each (30 roster people, a varied one from each place; 16 one-faced heroes; 4 givers), a session
   script plays a visit or two: greetings, small talk, questions about what is around, a place,
   the speaker, work, safety; thanks; an insult and an apology; a fright and calming words, while
   the fright lasts or after; work taken or turned down; and a second visit days later.
   `orchestrator_session` plays each against its world, and `communicator_eval --session ...
   --export-pairs` writes, for every turn a written line fits, the voice's own prompt
   (`Voice::messages`) and that line. The last world is held out.
2. **Training** (`train_lora.py`). LoRA of rank 16 (alpha 32, dropout 0.05) on every attention and
   feed-forward projection, 33 million weights, 0.8% of the model; the base in bf16, the adapter in
   fp32. Only the reply carries loss. At most 350 pairs a speaker's bank, so no one trade sets the
   style (the rosters lean on assistants and bakers). AdamW at 2e-4, cosine with 30 warm-up
   steps, batches of 4 accumulated to 16, 2 epochs, gradient checkpointing.
3. **The GGUF** (`to_gguf.py`). The adapter is merged into the base, llama.cpp's own converter
   (from the pinned source) writes a 16-bit GGUF, and llama.cpp's `llama-quantize` makes the
   Q4_K_M the runtime loads. The base alone goes through the same two steps, so the tuned model is
   compared with a base quantized the same way.

```bash
python -m venv training/.venv            # then install as requirements.txt says
training/.venv/Scripts/python training/make_pairs.py --session-tool <orchestrator_session.exe> \
    --eval <communicator_eval.exe> --lines communicator/data/lines --eval-lines communicator/eval/lines.txt \
    --out training/data --worlds 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115
training/.venv/Scripts/python training/train_lora.py --base inference/models/Qwen3-4B-Instruct-2507-hf \
    --data training/data --out training/out/lora
training/.venv/Scripts/python training/to_gguf.py --base inference/models/Qwen3-4B-Instruct-2507-hf \
    --lora training/out/lora --llama-src <llama.cpp source> --quantize <llama-quantize.exe> --out training/out/voice
```

## The data (2026-09-27)

6,947 pairs from 750 speakers, none of whose scripts failed: 6,524 to train from, and 423 from the
held-out world (seed 115). After the cap, 4,266 train (67 banks). Prompts run to a median of 224
tokens (the 95th percentile 762), replies to a median of 19. Outcomes, before the cap: answered 2,545, greeted
and parted 1,057 each, thanked 470, not understood 414, small talk 400, calmed 196, offended
181, forgiven 104, still afraid 58, work taken 25, turned down 17.

## The first run (2026-09-27)

534 steps, about two epochs, in 64 minutes on the RTX 5080 Laptop (about 11 GB in use). The loss
on the held-out world fell from 3.78 to 0.31 (0.73 at step 100, 0.49 at 200, 0.39 at 300, 0.34 at
400), level with the training loss at the end: it learned the lines' manner, not the pairs.

The tuned model (`inference/models/Qwen3-4B-Instruct-2507-voice-Q4_K_M.gguf`) against the base
quantized the same way, both through `communicator_eval` on this build; the GPU and the CPU gave
the same counts but two, shown apart:

| | Base, Q4_K_M (ours) | Tuned, Q4_K_M |
|---|---|---|
| Player lines read fully right, of 326 (GPU / CPU) | 245 / 246 | 251 / 251 |
| Unsafe reads | 1 | 2 |
| Voice replies holding their shape, of 59 | 59 | 59 |
| Replies with a claim the truth check finds (GPU / CPU) | 0 / 1 | 0 / 0 |
| Replies a blind judge found fault-free, of 59 | 27 | 44 |
| Replies inventing a fact (judge) | 20 | 3 |
| Replies missing what the speaker remembers (judge) | 8 | 6 |
| Tone faults (judge) | 5 | 0 |
| Craft, 1 to 5 (judge) | 2 | 3 |
| Mean time a line / a reply, GPU | 0.29 s / 0.29 s | 0.20 s / 0.23 s |

- **Invention fell from 20 replies to 3.** The base told of directions ("to the immediate left"),
  distances the packet does not hold, weather, keeps and hills, and read the people label as a
  temperament ("its people are cold"). Tuned: "This is Wenweg, and to be exact, the nearest is the
  ninth stone on the Linbrig road, a short walk away, dangerous, held by monsters..."; a bystander
  who joined: "I overheard that, and I'll answer it plainly."
- **Reading the player did not suffer:** one model reads and speaks, and it read 5 or 6 more
  lines right. It read one line more as world-changing: "ok", with work held and none just offered,
  as taking Nerbrann's work. The confirm step (`Interpretation::confirm`) asks before any work is
  taken, so it cannot change the world unasked.
- **What the tuning cost:** it speaks as the written lines do, so where they seldom recall
  something, it no longer does: what was asked last time, work finished, a talk-down (the base's
  own words had recalled them). It repeats a few lines across cases, and about six replies end in a
  muddled "and I'll not ___ it". The data had no pair where a bystander joins or someone talked down
  lately is calmed again at rest.
