"""The 51 spokes of the story web, its assignment equation and its typed constraints.

At 155 KB `story-tropes.json` is the largest authored document in the pair, and 98 % of
the data volume: 51 tropes x 3 acts x 317 options of prompts, completions and effect tags.
It is rendered as FLAT constexpr arrays with spans into them -- one option array, one act
array, one row per trope holding spans -- so nothing allocates and nothing is parsed at
runtime.

Four order contracts are live, and two are genuine tie-breaks rather than output cosmetics:

  * `requires` CONJUNCTION ORDER. The first conjunction wins a tie in two places, both with
    a strict less-than: the `missing` report takes the conjunction with the fewest failures,
    and a thread condition takes the cheapest way in. 28 of the 51 tropes have two or more
    conjunctions of equal length, so this decides real output.
  * `acts` ORDER. Position is the ring number and acts[0] is the resting hook. Exactly
    three are enforced.
  * `boosts` ORDER. `breakdown.boosted_by` is exported in it.
  * The trope list order, which is the order `tropes[]` is exported in.

The lints mirror the Python loader's -- which are strict, and are the reason the catalogue
can be trusted at runtime -- and add one it never had: a `fits` entry must be a REAL
archetype id. The reference only checked that it was a non-empty string, so a typo silently
removed a 1.6x multiplier and nothing said so.
"""
import json
import math

from embed.common import BANNER, OUT, cpp_double, cpp_string, load, sha256_of

TROPES = "story-tropes.json"
CONSTRAINTS = "story-constraints.json"
WEIGHTS = "story-weights.json"
TABLES = "story-code-tables.json"
ARCHETYPES = "hero-archetypes.json"

TROPE_FIELDS = frozenset(("id", "name", "pull", "requires", "boosts", "fits", "claims", "cast",
                          "acts", "examples"))
ACT_FIELDS = frozenset(("id", "title", "prompt", "completion", "abandonment", "options"))
OPTION_FIELDS = frozenset(("id", "text", "delta", "effect"))
CONSTRAINT_FIELDS = frozenset(("claim_locked", "drift_locked", "allowed_verbs"))
WEIGHT_VALUE_FIELDS = ("fit_gain", "fit_floor", "pull_floor", "fits_factor", "claim_factor",
                       "threshold", "node_intense_threshold", "famine_coverage",
                       "gainable_features", "max_threads_per_act", "initiative_days")
CAST_KINDS = frozenset(("person", "dread"))
ACTS_PER_TROPE = 3
MAX_DELTA = 0.5


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def derived_facts(tables):
    """The middle layer, DERIVED rather than carried.

    The document emits it for completeness under a name that says not to carry it: carrying
    it would leave three things to keep in step instead of one.
    """
    out = {"status:living", "status:legend", "home:set", "home:none", "lost:set", "faces:two",
           "companion:eligible", "war:veteran", "war:victor_recent", "war:defeated_recent"}
    out |= {"claim:" + v for v in tables["claim_verbs"]}
    out |= {"bond:" + k for k in tables["bond_kinds"]}
    out |= {"tier:" + t for t in tables["tiers"]}
    out |= {"presence:" + s for s in tables["situations"]}
    return out


def load_story_policies():
    tropes_doc = load(TROPES, 1)
    constraints = load(CONSTRAINTS, 1)
    weights = load(WEIGHTS, 1)
    tables = load(TABLES, 1)
    cards = load(ARCHETYPES, 1)["cards"]
    archetype_ids = {c["id"] for c in cards}

    hero_features = frozenset(tables["hero_features"])
    world_facts = frozenset(tables["world_facts"])
    facts = hero_features | world_facts | derived_facts(tables)
    if len(facts) != 151:
        raise SystemExit("the fact vocabulary derives to %d tokens, expected 151" % len(facts))
    # The document also carries the union it computed; if the two disagree the derivation
    # here and the reference's have drifted, which is exactly what this check is for.
    carried = frozenset(tables["facts_union_DERIVE_DO_NOT_CARRY"])
    if carried != facts:
        raise SystemExit("the derived fact union does not match the one the source recorded: %s"
                         % sorted(carried ^ facts))

    def known(token):
        bare = token[1:] if token.startswith("!") else token
        return bare in facts or (bare.startswith("archetype:") and len(bare) > len("archetype:"))

    rows = tropes_doc["tropes"]
    if not rows:
        raise SystemExit("story-tropes.json has no tropes; every hero would be unwoven")
    ids = [t["id"] for t in rows]
    if len(set(ids)) != len(ids):
        raise SystemExit("story-tropes.json has a duplicate trope id")
    for trope in rows:
        where = "trope " + trope.get("id", "<unnamed>")
        if set(trope) != TROPE_FIELDS:
            raise SystemExit("%s key set is %s" % (where, sorted(trope)))
        if set(trope["pull"]) != {"law", "good"}:
            raise SystemExit("%s pull must be exactly {law, good}" % where)
        for axis, value in trope["pull"].items():
            if not finite(value) or abs(value) > 1.0:
                raise SystemExit("%s pull.%s is %r, outside +/- 1" % (where, axis, value))
        if not trope["requires"] or any(not conj for conj in trope["requires"]):
            raise SystemExit("%s needs at least one non-empty conjunction" % where)
        for conj in trope["requires"]:
            for token in conj:
                if not known(token):
                    raise SystemExit("%s requires %r, which is not a known fact" % (where, token))
        for fact, factor in trope["boosts"].items():
            if not known(fact):
                raise SystemExit("%s boosts %r, which is not a known fact" % (where, fact))
            if not finite(factor) or factor <= 0:
                raise SystemExit("%s boosts %r by %r" % (where, fact, factor))
        for fit in trope["fits"]:
            if not fit:
                raise SystemExit("%s has an empty archetype in `fits`" % where)
            # NOT in the reference: a typo here silently removes the archetype multiplier.
            if fit not in archetype_ids:
                raise SystemExit("%s fits %r, which is not an archetype id" % (where, fit))
        for verb in trope["claims"]:
            if verb not in tables["claim_verbs"]:
                raise SystemExit("%s claims %r, which is not a claim verb" % (where, verb))
        if not trope["cast"] or not set(trope["cast"]) <= CAST_KINDS:
            raise SystemExit("%s cast must be a non-empty subset of %s" % (where, sorted(CAST_KINDS)))
        acts = trope["acts"]
        if len(acts) != ACTS_PER_TROPE:
            raise SystemExit("%s has %d acts, expected %d" % (where, len(acts), ACTS_PER_TROPE))
        if len({a["id"] for a in acts}) != ACTS_PER_TROPE:
            raise SystemExit("%s has a duplicate act id" % where)
        for act in acts:
            spot = "%s act %s" % (where, act.get("id"))
            if set(act) != ACT_FIELDS:
                raise SystemExit("%s key set is %s" % (spot, sorted(act)))
            # A STRING, possibly empty -- which is the reference's own check, and it
            # matters: `the_lair`, the only dread-only trope, ships all three acts with an
            # EMPTY prompt, so a Dread offered it gets a resting hook with nothing to read.
            # Tightening this here would refuse the shipped catalogue rather than fix it.
            if not act["title"] or not isinstance(act["prompt"], str):
                raise SystemExit("%s needs a title and a prompt string" % spot)
            completion = act["completion"]
            if completion.get("kind") != "target" or not completion.get("target") \
                    or not completion.get("state"):
                raise SystemExit("%s completion must be a named target in a named state" % spot)
            abandonment = act["abandonment"]
            if abandonment.get("kind") != "deadline" or not isinstance(abandonment.get("days"), int) \
                    or abandonment["days"] < 1:
                raise SystemExit("%s abandonment must be a deadline of at least one day" % spot)
            options = act["options"]
            if not 2 <= len(options) <= 4:
                raise SystemExit("%s has %d options, expected two to four" % (spot, len(options)))
            if len({o["id"] for o in options}) != len(options):
                raise SystemExit("%s has a duplicate option id" % spot)
            for option in options:
                if set(option) != OPTION_FIELDS:
                    raise SystemExit("%s option %s key set is %s"
                                     % (spot, option.get("id"), sorted(option)))
                if not option["text"] or not option["effect"]:
                    raise SystemExit("%s option %s needs text and an effect" % (spot, option["id"]))
                if set(option["delta"]) != {"law", "good"}:
                    raise SystemExit("%s option %s delta must be exactly {law, good}"
                                     % (spot, option["id"]))
                for axis, value in option["delta"].items():
                    if not finite(value) or abs(value) > MAX_DELTA:
                        raise SystemExit("%s option %s delta.%s is %r, outside +/- %r"
                                         % (spot, option["id"], axis, value, MAX_DELTA))
        if not trope["examples"] or any(not e for e in trope["examples"]):
            raise SystemExit("%s needs at least one non-empty prose example" % where)

    for field in WEIGHT_VALUE_FIELDS:
        if field not in weights:
            raise SystemExit("story-weights.json lost %r" % field)
    for token in weights["gainable_features"]:
        if not known(token):
            raise SystemExit("gainable_features names %r, which is not a known fact" % token)
    if weights["max_threads_per_act"] < 1:
        raise SystemExit("max_threads_per_act must admit at least one thread")
    if weights["fit_floor"] <= 0:
        raise SystemExit("fit_floor damps an opposed spoke; a floor of zero would REMOVE it")

    if set(constraints["default"]) != CONSTRAINT_FIELDS:
        raise SystemExit("the default constraint key set is %s" % sorted(constraints["default"]))
    for name, entry in constraints["archetypes"].items():
        if name not in archetype_ids:
            raise SystemExit("constraints name %r, which is not an archetype id" % name)
        if set(entry) != CONSTRAINT_FIELDS:
            raise SystemExit("constraint %s key set is %s" % (name, sorted(entry)))
        if entry["allowed_verbs"] is not None:
            if not entry["allowed_verbs"]:
                raise SystemExit("constraint %s allows an EMPTY verb list; null means any verb "
                                 "and [] would mean none, so an empty list is almost certainly "
                                 "a mistake" % name)
            for verb in entry["allowed_verbs"]:
                if verb not in tables["claim_verbs"]:
                    raise SystemExit("constraint %s allows %r, which is not a claim verb"
                                     % (name, verb))

    for token in tables["never_computed"]:
        if token not in hero_features:
            raise SystemExit("never_computed names %r, which is not even a hero feature" % token)
    if len(tables["situations"]) != 19:
        raise SystemExit("the presence vocabulary is %d situations, expected 19"
                         % len(tables["situations"]))
    return tropes_doc, constraints, weights, tables, sorted(facts)


def chunked(indent, items, per_line=4):
    return [indent + " ".join(items[i:i + per_line]) for i in range(0, len(items), per_line)]


def render_story_web():
    tropes_doc, constraints, weights, tables, facts = load_story_policies()
    rows = tropes_doc["tropes"]

    tokens, clauses, boosts, texts = [], [], [], []
    options, acts = [], []
    trope_rows = []

    def pool(values):
        begin = len(texts)
        texts.extend(values)
        return "{kText + %d, %d}" % (begin, len(values))

    for trope in rows:
        clause_begin = len(clauses)
        for conj in trope["requires"]:
            begin = len(tokens)
            tokens.extend(conj)
            clauses.append("{{kReqTokens + %d, %d}}" % (begin, len(conj)))
        boost_begin = len(boosts)
        for fact, factor in trope["boosts"].items():
            boosts.append("{%s, %s}" % (cpp_string(fact), cpp_double(factor)))
        fits = pool(trope["fits"])
        claims = pool(trope["claims"])
        cast = pool(trope["cast"])
        examples = pool(trope["examples"])
        act_begin = len(acts)
        for act in trope["acts"]:
            option_begin = len(options)
            for option in act["options"]:
                options.append("    {%s, %s,\n     {%s, %s}, %s},"
                               % (cpp_string(option["id"]), cpp_string(option["text"]),
                                  cpp_double(option["delta"]["law"]),
                                  cpp_double(option["delta"]["good"]),
                                  cpp_string(option["effect"])))
            acts.append("    {%s, %s,\n     %s,\n     %s, %s, %d,\n     {kOptions + %d, %d}},"
                        % (cpp_string(act["id"]), cpp_string(act["title"]),
                           cpp_string(act["prompt"]),
                           cpp_string(act["completion"]["target"]),
                           cpp_string(act["completion"]["state"]),
                           act["abandonment"]["days"], option_begin,
                           len(options) - option_begin))
        trope_rows.append(dict(
            trope=trope,
            clauses="{kClauses + %d, %d}" % (clause_begin, len(clauses) - clause_begin),
            boosts="{kBoosts + %d, %d}" % (boost_begin, len(boosts) - boost_begin),
            fits=fits, claims=claims, cast=cast, examples=examples,
            acts="{kActs + %d, %d}" % (act_begin, len(acts) - act_begin)))

    out = [BANNER.format(source="story-tropes.json, story-constraints.json, "
                                "story-weights.json and story-code-tables.json")]
    out.append('#include "fwg/story_web.hpp"\n')
    out.append("#include <algorithm>\n")
    out.append("namespace fwg {")
    out.append("namespace {\n")

    out.append("// A shared pool for the short string lists a trope carries: fits, claims, cast")
    out.append("// and examples. One array rather than four, because none of them is indexed")
    out.append("// positionally -- only membership and export order matter.")
    out.append("constexpr std::string_view kText[] = {")
    out.extend(chunked("    ", ["%s," % cpp_string(t) for t in texts], 2))
    out.append("};\n")

    out.append("// Precondition tokens. A leading '!' makes the fact's ABSENCE the condition.")
    out.append("constexpr std::string_view kReqTokens[] = {")
    out.extend(chunked("    ", ["%s," % cpp_string(t) for t in tokens], 4))
    out.append("};\n")
    out.append("// CONJUNCTION ORDER IS A CONTRACT: the first conjunction wins a tie in the")
    out.append("// `missing` report and in the thread walk, both with a strict less-than.")
    out.append("constexpr HeroClause kClauses[] = {")
    out.extend(chunked("    ", ["%s," % c for c in clauses], 3))
    out.append("};\n")
    out.append("// FILE ORDER: breakdown.boosted_by is exported in it.")
    out.append("constexpr HeroBoost kBoosts[] = {")
    out.extend(chunked("    ", ["%s," % b for b in boosts], 2))
    out.append("};\n")

    out.append("constexpr StoryOption kOptions[] = {")
    out.extend(options)
    out.append("};\n")
    out.append("// POSITION IS THE RING NUMBER, and acts[0] of the offered trope is the hero's")
    out.append("// resting hook. Exactly three per trope.")
    out.append("constexpr StoryAct kActs[] = {")
    out.extend(acts)
    out.append("};\n")

    out.append("constexpr StoryTrope kTropes[] = {")
    for row in trope_rows:
        t = row["trope"]
        out.append("    {%s, %s, {%s, %s},"
                   % (cpp_string(t["id"]), cpp_string(t["name"]),
                      cpp_double(t["pull"]["law"]), cpp_double(t["pull"]["good"])))
        out.append("     %s, %s," % (row["clauses"], row["boosts"]))
        out.append("     %s, %s, %s," % (row["fits"], row["claims"], row["cast"]))
        out.append("     %s, %s}," % (row["acts"], row["examples"]))
    out.append("};\n")

    out.append("constexpr StoryWeightPolicy kWeights = {%s, %s, %s, %s, %s, %s, %s, %s, %d, %d};"
               % (cpp_double(weights["fit_gain"]), cpp_double(weights["fit_floor"]),
                  cpp_double(weights["pull_floor"]), cpp_double(weights["fits_factor"]),
                  cpp_double(weights["claim_factor"]), cpp_double(weights["threshold"]),
                  cpp_double(weights["node_intense_threshold"]),
                  cpp_double(weights["famine_coverage"]),
                  weights["max_threads_per_act"], weights["initiative_days"]))
    out.append("constexpr std::string_view kGainable[] = {")
    out.extend(chunked("    ", ["%s," % cpp_string(g) for g in weights["gainable_features"]], 3))
    out.append("};\n")

    verbs_pool = []
    constraint_rows = []

    def constraint_literal(name, entry):
        if entry["allowed_verbs"] is None:
            span = "{}"
            has = "false"
        else:
            begin = len(verbs_pool)
            verbs_pool.extend(entry["allowed_verbs"])
            span = "{kAllowedVerbs + %d, %d}" % (begin, len(entry["allowed_verbs"]))
            has = "true"
        return ("    {%s, %s, %s, %s, %s},"
                % (cpp_string(name), "true" if entry["claim_locked"] else "false",
                   "true" if entry["drift_locked"] else "false", has, span))

    constraint_rows.append(constraint_literal("", constraints["default"]))
    for name, entry in constraints["archetypes"].items():
        constraint_rows.append(constraint_literal(name, entry))
    out.append("constexpr std::string_view kAllowedVerbs[] = {%s};"
               % ", ".join(cpp_string(v) for v in verbs_pool))
    out.append("// Row 0 is the DEFAULT, which is unconstrained rather than locked; an archetype")
    out.append("// with no row of its own -- and a hero with no archetype at all -- lands on it.")
    out.append("constexpr StoryConstraint kConstraints[] = {")
    out.extend(constraint_rows)
    out.append("};\n")

    def text_list(name, values, per_line=3):
        out.append("constexpr std::string_view %s[] = {" % name)
        out.extend(chunked("    ", ["%s," % cpp_string(v) for v in values], per_line))
        out.append("};")

    out.append("// THE SECOND COPY of the hero feature vocabulary, from this package's own")
    out.append("// document. A test ties it to the hero generator's; the reference held the two")
    out.append("// in sync by comment alone.")
    text_list("kHeroFeatures", tables["hero_features"])
    text_list("kWorldFacts", tables["world_facts"])
    out.append("// The union, DERIVED at generation time and emitted sorted so a lookup can")
    out.append("// binary-search it. Carrying the source's own copy would leave three things to")
    out.append("// keep in step instead of one.")
    text_list("kFacts", facts)
    text_list("kClaimVerbs", tables["claim_verbs"], 4)
    text_list("kBondKinds", tables["bond_kinds"], 4)
    text_list("kSituations", tables["situations"], 4)
    text_list("kNeverComputed", tables["never_computed"])
    out.append("")
    text_list("kTiers", tables["tiers"], 3)
    out.append("")
    out.append("constexpr StoryFallback kFallbacks[] = {%s};"
               % ", ".join("{%s, %s}" % (cpp_string(k), cpp_string(v))
                           for k, v in tables["bind_fallback"].items()))
    out.append("// A POSITIONAL RANK, and the tie-break that decides which conjunction a thread")
    out.append("// condition names as well as the primary key of the final thread sort.")
    out.append("struct RankRow { std::string_view kind; int rank; };")
    out.append("constexpr RankRow kThreadRank[] = {%s};"
               % ", ".join("{%s, %d}" % (cpp_string(k), v)
                           for k, v in tables["thread_rank"].items()))
    out.append("")
    out.append("constexpr std::string_view kMethod =")
    out.append("    %s;" % cpp_string(tables["method"]))
    out.append("constexpr std::string_view kLimits =")
    out.append("    %s;" % cpp_string(tables["limits"]))
    out.append("")
    out.append("}  // namespace\n")

    out.append("std::span<const StoryTrope> story_tropes() { return kTropes; }\n")
    out.append("""const StoryTrope* story_trope(std::string_view id) {
    for (const StoryTrope& trope : kTropes) {
        if (trope.id == id) return &trope;
    }
    return nullptr;
}
""")
    out.append("const StoryWeightPolicy& story_weight_policy() { return kWeights; }")
    out.append("std::span<const std::string_view> story_gainable_features() { return kGainable; }\n")
    out.append("""const StoryConstraint& story_constraints_for(std::string_view archetype) {
    if (!archetype.empty()) {
        for (const StoryConstraint& row : kConstraints) {
            if (row.archetype == archetype) return row;
        }
    }
    return kConstraints[0];
}
""")
    out.append("std::span<const std::string_view> story_hero_features() { return kHeroFeatures; }")
    out.append("std::span<const std::string_view> story_world_facts() { return kWorldFacts; }")
    out.append("std::span<const std::string_view> story_facts() { return kFacts; }")
    out.append("std::span<const std::string_view> story_claim_verbs() { return kClaimVerbs; }")
    out.append("std::span<const std::string_view> story_bond_kinds() { return kBondKinds; }")
    out.append("std::span<const std::string_view> story_tiers() { return kTiers; }")
    out.append("std::span<const std::string_view> story_situations() { return kSituations; }")
    out.append("std::span<const std::string_view> story_never_computed() { return kNeverComputed; }")
    out.append("std::span<const StoryFallback> story_bind_fallbacks() { return kFallbacks; }\n")
    out.append("""bool story_known_fact(std::string_view token) {
    if (std::binary_search(std::begin(kFacts), std::end(kFacts), token)) return true;
    constexpr std::string_view kPrefix = "archetype:";
    return token.size() > kPrefix.size() && token.substr(0, kPrefix.size()) == kPrefix;
}

int story_thread_rank(std::string_view kind) {
    for (const RankRow& row : kThreadRank) {
        if (row.kind == kind) return row.rank;
    }
    return -1;
}
""")
    digest = sha256_of("".join(sorted(str(load(name, 1)) for name in
                                      (TROPES, CONSTRAINTS, WEIGHTS, TABLES))))
    out.append("""StoryCatalogueIdentity story_catalogue_identity() {
    return {%d, %d, %d, %d, %s};
}
""" % (1, tropes_doc["source_identity"]["revision"], constraints["source_identity"]["revision"],
       weights["source_identity"]["revision"], cpp_string(digest)))
    out.append("std::string_view story_web_method() { return kMethod; }")
    out.append("std::string_view story_web_limits() { return kLimits; }\n")
    out.append("}  // namespace fwg")
    assert json  # the loader is the whole of the validation above
    return "\n".join(out) + "\n"


TARGETS = {OUT / "content_story_web.cpp": render_story_web}
