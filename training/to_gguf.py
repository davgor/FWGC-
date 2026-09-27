"""Merge a trained LoRA into the base model and make the GGUF the runtime loads.

1. The adapter is merged into the base weights (bf16) and saved as a Hugging Face model.
2. llama.cpp's own converter (convert_hf_to_gguf.py, from the pinned source the inference build
   fetches) writes it as a 16-bit GGUF.
3. llama.cpp's llama-quantize, built from the same source, quantizes it to Q4_K_M, the base's
   quantization.

    python to_gguf.py --base PATH --lora out/lora --llama-src PATH --quantize PATH --out out/voice

With --no-lora, the base alone goes through steps 2 and 3, so the tuned model can be compared
with a base quantized the same way. The result is copied into inference/models by hand: nothing
that ships reads training/.
"""
import argparse
import os
import shutil
import subprocess
import sys

import torch


def run(args, env=None):
    print("+", " ".join(args), flush=True)
    r = subprocess.run(args, env=env)
    if r.returncode != 0:
        sys.exit(f"failed ({r.returncode}): {args[0]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="the base model's Hugging Face folder")
    ap.add_argument("--lora", help="the adapter train_lora.py saved")
    ap.add_argument("--no-lora", action="store_true", help="convert the base alone, for a like-for-like comparison")
    ap.add_argument("--llama-src", required=True, help="the pinned llama.cpp source (convert_hf_to_gguf.py)")
    ap.add_argument("--quantize", required=True, help="llama-quantize.exe built from the same source")
    ap.add_argument("--out", required=True, help="a folder for the merged model and the GGUFs")
    ap.add_argument("--name", default="voice")
    a = ap.parse_args()
    if bool(a.lora) == a.no_lora:
        sys.exit("give --lora or --no-lora")
    os.makedirs(a.out, exist_ok=True)

    source = a.base
    if a.lora:
        from peft import PeftModel
        from transformers import AutoModelForCausalLM, AutoTokenizer

        merged = os.path.join(a.out, "merged")
        model = AutoModelForCausalLM.from_pretrained(a.base, dtype=torch.bfloat16)
        model = PeftModel.from_pretrained(model, a.lora).merge_and_unload()
        model.save_pretrained(merged, safe_serialization=True)
        AutoTokenizer.from_pretrained(a.base).save_pretrained(merged)
        for extra in ("generation_config.json", "LICENSE"):
            if os.path.exists(os.path.join(a.base, extra)):
                shutil.copy(os.path.join(a.base, extra), merged)
        source = merged
        del model

    f16 = os.path.join(a.out, f"{a.name}-f16.gguf")
    env = dict(os.environ, PYTHONPATH=os.path.join(a.llama_src, "gguf-py") + os.pathsep + a.llama_src, NO_LOCAL_GGUF="")
    run([sys.executable, os.path.join(a.llama_src, "convert_hf_to_gguf.py"), source, "--outtype", "f16", "--outfile", f16], env)
    q4 = os.path.join(a.out, f"{a.name}-Q4_K_M.gguf")
    run([a.quantize, f16, q4, "Q4_K_M"])
    print(f"wrote {q4} ({os.path.getsize(q4):,} bytes)")


if __name__ == "__main__":
    main()
