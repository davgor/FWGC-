"""Training pairs for the voice: the prompt the game's voice would get, and the written line the
game would say, over many real speakers.

For each world and each speaker picked from it, a session script (orchestrator/tools/session.cpp
says how one reads) plays a visit or two: greetings, small talk, questions, thanks, an insult and
an apology, a fright and calming words, work asked for and taken. orchestrator_session plays it
against the generated world and writes each turn's packet; communicator_eval --session speaks the
packets with the written lines and --export-pairs writes, for each turn a line fits, the voice's
own messages and that line. A script that asks for what its world does not hold (a giver with no
work of their own) fails, and is dropped.

    python make_pairs.py --session-tool PATH --eval PATH --lines PATH --out data/

Writes data/pairs_train.jsonl and data/pairs_val.jsonl (the last world held out), and
data/scripts/ with every script played.
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys

# The player's side: generic lines, with no name or trade a speaker might not hold. The eval's
# labelled lines (communicator/eval/lines.txt) that fit are added to these.
PLAYER = {
    "greet": ["Hello.", "Good day.", "Hi there.", "Well met.", "Morning.", "Greetings.", "Hello again."],
    "small_talk": ["Quiet today, isn't it?", "How's the work going?", "Cold one, eh?", "Busy day?", "Nice weather.",
                   "How are things around here?", "Quite a day, eh?"],
    "ask_surroundings": ["What's around here?", "What lies nearby?", "Where am I, exactly?", "What's close by?",
                         "Anything worth seeing near here?"],
    "ask_safety": ["Is it safe around here?", "Any dangers I should know about?", "Is it safe to travel near here?",
                   "What should I watch out for?"],
    "ask_for_work": ["Any work going?", "Got anything for me to do?", "Is anyone hiring?", "Need a hand with anything?"],
    "thank": ["Thank you.", "Thanks.", "Much obliged.", "Thank you kindly."],
    "farewell": ["Goodbye.", "Farewell.", "I'll be on my way.", "See you around.", "Take care."],
    "insult": ["You're useless.", "What a waste of breath you are.", "Out of my way, fool.", "You smell like a goat."],
    "apologize": ["I'm sorry for what I said.", "Forgive me, I was rude.", "My apologies.", "Sorry, I was out of line."],
    "calm": ["It's all right, you're safe now.", "Breathe. It's over.", "Easy, easy. You're safe.", "Don't be afraid."],
    "no_act": ["What's the capital of the moon?", "Sing me a song.", "asdf qwerty", "Give me a thousand gold."],
}
ABOUT_PLACE = ["Tell me about that place.", "What do you know of it?", "What's that place like?"]
ABOUT_SELF = ["Who are you?", "What do you do here?", "Tell me about yourself."]
ACCEPT = ["I'll do it.", "Count me in.", "I'll take that on."]
DECLINE = ["Not for me.", "I'll pass.", "No, thank you."]
ROLE_WORDS = {"castellan", "captain", "keeper", "innkeeper", "hakndum", "sir", "madam", "barkeep"}


def eval_lines(path):
    """The eval's lines with no target, no context and no capitalised word past the first."""
    out = {}
    for raw in open(path, encoding="utf-8"):
        if raw.lstrip().startswith("#"):
            continue
        cells = [c.strip() for c in raw.rstrip("\n").split("|")]
        if len(cells) < 6:
            continue
        _, last, said, act, target, tags = cells[:6]
        words = re.findall(r"[A-Za-z']+", said)
        if target != "-" or last != "-" or "context" in tags or act not in PLAYER:
            continue
        if any(w[0].isupper() and w != "I" for w in words[1:]) or ROLE_WORDS & {w.lower() for w in words}:
            continue
        out.setdefault(act, []).append(said)
    return out


def say(name, act, rng, target=""):
    line = rng.choice(PLAYER[act])
    return f"say {name} {act}{(' ' + target) if target else ''} | {line}"


def visit(name, rng, kind):
    """One conversation with a speaker: a greeting, a few things asked, maybe a quarrel, goodbye."""
    rows = [say(name, "greet", rng)]
    asks = ["small_talk", "ask_surroundings", "ask_safety", "ask_for_work", "about_place", "about_self", "no_act"]
    rng.shuffle(asks)
    for ask in asks[: rng.randint(2, 4)]:
        if ask == "about_place":
            rows.append(say(name, "ask_surroundings", rng))
            rows.append(f"say {name} ask_about_place @place | {rng.choice(ABOUT_PLACE)}")
        elif ask == "about_self":
            rows.append(f"say {name} ask_about_person {name} | {rng.choice(ABOUT_SELF)}")
        else:
            rows.append(say(name, ask, rng))
    if rng.random() < 0.2:
        rows.append(say(name, "insult", rng))
        if rng.random() < 0.6:
            rows.append(say(name, "apologize", rng))
    elif rng.random() < 0.5:
        rows.append(say(name, "thank", rng))
    if kind == "giver" and rng.random() < 0.8:
        rows.append(say(name, "ask_for_work", rng))
        take = rng.random() < 0.6
        rows.append(f"say {name} {'accept_quest' if take else 'decline_quest'} @own | "
                    f"{rng.choice(ACCEPT) if take else rng.choice(DECLINE)}")
    rows.append(say(name, "farewell", rng))
    return rows


def frightened(name, rng):
    """Something frightens the speaker; the player greets them, asks, and talks them down."""
    # Mostly once what frightened them has passed (calmed), sometimes while it is still there
    # (still afraid: the threat lasts a quarter of an hour).
    rows = [f"fright {name} {rng.choice([2, 3])}"] + (["minutes 16"] if rng.random() < 0.7 else []) + [say(name, "greet", rng)]
    if rng.random() < 0.7:
        rows.append(say(name, "ask_surroundings", rng))
    rows += [say(name, "calm", rng), say(name, "calm", rng)]
    if rng.random() < 0.5:
        rows.append(say(name, "thank", rng))
    rows.append(say(name, "farewell", rng))
    return rows


def script(seed, kind, index, rng):
    name = "s"
    rows = [f"world {seed} 33 30"]
    if kind == "townsfolk":
        # townsfolk picks the first roster person of each place, mostly one trade (a first set was
        # 28% armorers): someone else from the same place instead.
        rows += [f"who t {kind} {index}", f"who {name} near t {rng.randint(0, 6)}"]
    else:
        rows.append(f"who {name} {kind} {index}")
    if kind == "townsfolk" and rng.random() < 0.3:
        rows += frightened(name, rng)
    else:
        rows += visit(name, rng, kind)
    # Met again, some days on: greeted as known, asked again.
    if rng.random() < 0.5:
        rows.append(f"advance {rng.choice([1, 3, 9, 20])}")
        rows += visit(name, rng, "again")
    return rows


def run(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--session-tool", required=True, help="orchestrator_session.exe")
    ap.add_argument("--eval", required=True, help="communicator_eval.exe")
    ap.add_argument("--lines", required=True, help="communicator/data/lines")
    ap.add_argument("--eval-lines", required=True, help="communicator/eval/lines.txt, for more player lines")
    ap.add_argument("--out", required=True)
    ap.add_argument("--worlds", type=int, nargs="+", default=[101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111])
    ap.add_argument("--townsfolk", type=int, default=30, help="townsfolk a world")
    ap.add_argument("--heroes", type=int, default=16, help="one-faced heroes a world")
    ap.add_argument("--givers", type=int, default=4, help="givers a world")
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()

    # Only where a line fits any speaker: a question the eval asked in its own context ("those
    # glacier dragons, how close are they") would not match another speaker's packet.
    for act, lines in eval_lines(a.eval_lines).items():
        if act in {"greet", "thank", "farewell", "apologize", "insult", "calm", "small_talk"}:
            PLAYER[act] = sorted(set(PLAYER[act]) | set(lines))
    scripts_dir = os.path.join(a.out, "scripts")
    os.makedirs(scripts_dir, exist_ok=True)
    rng = random.Random(a.seed)
    held_out = a.worlds[-1]
    counts = {"played": 0, "failed": 0, "pairs": 0}
    with open(os.path.join(a.out, "pairs_train.jsonl"), "w", encoding="utf-8") as train, \
         open(os.path.join(a.out, "pairs_val.jsonl"), "w", encoding="utf-8") as val:
        for seed in a.worlds:
            plan = [("townsfolk", i) for i in range(a.townsfolk)] + [("plain", i) for i in range(a.heroes)] + \
                   [("giver", i) for i in range(a.givers)]
            for kind, index in plan:
                name = f"w{seed}_{kind}{index}"
                path = os.path.join(scripts_dir, name + ".txt")
                with open(path, "w", encoding="utf-8") as f:
                    f.write("\n".join(script(seed, kind, index, rng)) + "\n")
                packets = os.path.join(scripts_dir, name + ".packets")
                r = run([a.session_tool, path])
                if r.returncode != 0:
                    counts["failed"] += 1
                    continue
                with open(packets, "w", encoding="utf-8") as f:
                    f.write(r.stdout)
                pairs = os.path.join(scripts_dir, name + ".jsonl")
                r = run([a.eval, "--session", packets, "--authored", a.lines, "--export-pairs", pairs])
                if r.returncode != 0:
                    print("export failed:", name, r.stderr[-300:], file=sys.stderr)
                    counts["failed"] += 1
                    continue
                counts["played"] += 1
                sink = val if seed == held_out else train
                for row in open(pairs, encoding="utf-8"):
                    record = json.loads(row)
                    record["world"] = seed
                    record["script"] = name
                    sink.write(json.dumps(record, ensure_ascii=False) + "\n")
                    counts["pairs"] += 1
            print(f"world {seed}: {counts}", flush=True)
    print(json.dumps(counts))


if __name__ == "__main__":
    main()
