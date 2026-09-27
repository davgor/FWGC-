"""Train a LoRA for the voice on the written lines (make_pairs.py's pairs).

Each pair is the voice's own prompt, rendered with the model's chat template (plain ChatML, the
layout llama.cpp gives it at run time), and the written reply the game would say, closed with the
end-of-turn token. Only the reply is learned: the prompt's tokens carry no loss, and logits are
computed at the reply's positions alone (over the whole prompt they would take gigabytes).

    python train_lora.py --base PATH --data data/ --out out/lora

Writes the adapter (PEFT) and out/lora/log.jsonl, one line per logged step and evaluation.
"""
import argparse
import json
import math
import os
import random
import time

import torch
import torch.nn.functional as F
from peft import LoraConfig, get_peft_model
from transformers import AutoModelForCausalLM, AutoTokenizer

END = "<|im_end|>"


def load_pairs(path):
    return [json.loads(line) for line in open(path, encoding="utf-8")]


def capped(pairs, cap, rng):
    """At most `cap` pairs a speaker's bank, so no one voice sets the style: the worlds' rosters
    lean on a few trades (a first set was a fifth assistants and a fifth bakers)."""
    banks = {}
    for pair in pairs:
        banks.setdefault(pair["voice_key"] or "post:" + pair["role"], []).append(pair)
    out = []
    for bank in sorted(banks):
        rows = banks[bank]
        rng.shuffle(rows)
        out += rows[:cap]
    return out


def encode(tok, pair, max_tokens):
    prompt = tok.apply_chat_template([{"role": "system", "content": pair["system"]}, {"role": "user", "content": pair["user"]}],
                                     tokenize=False, add_generation_prompt=True)
    prompt_ids = tok(prompt, add_special_tokens=False)["input_ids"]
    reply_ids = tok(pair["response"] + END, add_special_tokens=False)["input_ids"]
    ids = (prompt_ids + reply_ids)[:max_tokens]
    labels = ([-100] * len(prompt_ids) + reply_ids)[:max_tokens]
    return ids, labels


def batches(examples, size, pad_id, rng, shuffle):
    order = list(range(len(examples)))
    if shuffle:
        rng.shuffle(order)
    for start in range(0, len(order), size):
        chunk = [examples[i] for i in order[start:start + size]]
        width = max(len(ids) for ids, _ in chunk)
        ids = torch.full((len(chunk), width), pad_id, dtype=torch.long)
        labels = torch.full((len(chunk), width), -100, dtype=torch.long)
        mask = torch.zeros((len(chunk), width), dtype=torch.long)
        for row, (i, l) in enumerate(chunk):
            ids[row, : len(i)] = torch.tensor(i)
            labels[row, : len(l)] = torch.tensor(l)
            mask[row, : len(i)] = 1
        yield ids, mask, labels


def reply_loss(model, ids, mask, labels):
    """Cross-entropy over the reply's tokens only, with logits computed there alone."""
    base = model.get_base_model()
    hidden = base.model(input_ids=ids, attention_mask=mask).last_hidden_state
    shifted = labels[:, 1:]
    keep = shifted != -100
    logits = base.lm_head(hidden[:, :-1, :][keep])
    return F.cross_entropy(logits.float(), shifted[keep]), int(keep.sum())


@torch.no_grad()
def evaluate(model, examples, pad_id, device, size):
    model.eval()
    total, count = 0.0, 0
    for ids, mask, labels in batches(examples, size, pad_id, random.Random(0), False):
        with torch.autocast("cuda", dtype=torch.bfloat16):
            loss, n = reply_loss(model, ids.to(device), mask.to(device), labels.to(device))
        total += float(loss) * n
        count += n
    model.train()
    return total / max(count, 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="the base model's Hugging Face folder")
    ap.add_argument("--data", required=True, help="make_pairs.py's output folder")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rank", type=int, default=16)
    ap.add_argument("--alpha", type=int, default=32)
    ap.add_argument("--dropout", type=float, default=0.05)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--epochs", type=float, default=2.0)
    ap.add_argument("--batch", type=int, default=4)
    ap.add_argument("--accumulate", type=int, default=4)
    ap.add_argument("--max-tokens", type=int, default=1280)
    ap.add_argument("--warmup", type=int, default=30)
    ap.add_argument("--eval-every", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cap", type=int, default=350, help="at most this many training pairs a bank")
    a = ap.parse_args()

    torch.manual_seed(a.seed)
    rng = random.Random(a.seed)
    device = "cuda"
    tok = AutoTokenizer.from_pretrained(a.base)
    train = [encode(tok, p, a.max_tokens) for p in capped(load_pairs(os.path.join(a.data, "pairs_train.jsonl")), a.cap, rng)]
    val = [encode(tok, p, a.max_tokens) for p in load_pairs(os.path.join(a.data, "pairs_val.jsonl"))]
    print(f"{len(train)} training pairs, {len(val)} held out", flush=True)

    model = AutoModelForCausalLM.from_pretrained(a.base, dtype=torch.bfloat16, device_map={"": device})
    model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={"use_reentrant": False})
    model.enable_input_require_grads()
    config = LoraConfig(r=a.rank, lora_alpha=a.alpha, lora_dropout=a.dropout, task_type="CAUSAL_LM",
                        target_modules=["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"])
    model = get_peft_model(model, config)
    for p in model.parameters():  # the adapter learns in full precision; the base stays bf16
        if p.requires_grad:
            p.data = p.data.float()
    model.print_trainable_parameters()

    params = [p for p in model.parameters() if p.requires_grad]
    optimizer = torch.optim.AdamW(params, lr=a.lr, weight_decay=0.0)
    steps_per_epoch = math.ceil(len(train) / (a.batch * a.accumulate))
    total_steps = int(steps_per_epoch * a.epochs)

    def lr_at(step):
        if step < a.warmup:
            return a.lr * (step + 1) / a.warmup
        progress = (step - a.warmup) / max(1, total_steps - a.warmup)
        return a.lr * 0.5 * (1 + math.cos(math.pi * min(1.0, progress)))

    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "log.jsonl"), "w", encoding="utf-8")
    pad_id = tok.pad_token_id
    first = evaluate(model, val, pad_id, device, a.batch)
    print(f"held-out loss before training: {first:.4f}", flush=True)
    log.write(json.dumps({"step": 0, "val_loss": first}) + "\n")
    step, started, running = 0, time.time(), []
    model.train()
    epoch = 0
    while step < total_steps:
        micro = 0
        for ids, mask, labels in batches(train, a.batch, pad_id, rng, True):
            with torch.autocast("cuda", dtype=torch.bfloat16):
                loss, _ = reply_loss(model, ids.to(device), mask.to(device), labels.to(device))
            (loss / a.accumulate).backward()
            running.append(float(loss))
            micro += 1
            if micro % a.accumulate:
                continue
            for group in optimizer.param_groups:
                group["lr"] = lr_at(step)
            torch.nn.utils.clip_grad_norm_(params, 1.0)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
            step += 1
            if step % 10 == 0:
                record = {"step": step, "loss": sum(running) / len(running), "lr": lr_at(step),
                          "seconds": round(time.time() - started, 1)}
                running = []
                log.write(json.dumps(record) + "\n")
                log.flush()
                print(record, flush=True)
            if step % a.eval_every == 0 or step == total_steps:
                held = evaluate(model, val, pad_id, device, a.batch)
                log.write(json.dumps({"step": step, "val_loss": held}) + "\n")
                log.flush()
                print(f"step {step}: held-out loss {held:.4f}", flush=True)
            if step >= total_steps:
                break
        epoch += 1
    model.save_pretrained(a.out)
    tok.save_pretrained(a.out)
    print(f"saved to {a.out} after {step} steps, {epoch} epochs, {time.time() - started:.0f} s", flush=True)


if __name__ == "__main__":
    main()
