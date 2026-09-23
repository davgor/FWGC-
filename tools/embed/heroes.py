"""The hero generator's five policy documents plus the tables that lived in its code.

Rendered as flat constexpr arrays with spans into them, the way the creature catalogue
is: nothing allocates and nothing is parsed at runtime. The prose is the bulk of it --
32 cards' belief/wants/fears/tells/mechanic/never/pole_notes/lines and six axis overlays
ARE the exported persona, so this file carries about 60 KB of authored English.

Four orderings reach generated worlds and are preserved deliberately:

  * CARD ORDER. `choose_archetype` collects the eligible cards in catalogue order and
    walks them with a subtractive cumulative selector, so the same rows in a different
    order are a different cast on the same seed. Nothing sorts first.
  * `lines` ORDER inside a card. The persona's sample utterance falls back to the FIRST
    entry when neither the exact alignment code nor the good pole matches, and 27 of the
    32 cards carry no neutral-good line, so that first entry is what every neutral-good
    person in every world says.
  * `boosts` ORDER inside a card. The weight is a float product over the terms and
    multiplication does not associate, so a reordered table can move the last bit and
    flip a knife-edge draw.
  * EVERY LIST in names.json. Epithets and cover roles are drawn by position, and the
    epithet filter is order-preserving, so both the full list and every surviving
    subsequence are contracts. The `legend` list is appended at the tail.

The lints mirror the Python loader's and add two it never had. `lint_archetypes` compared
a card's key set for exact equality but NEVER checked that a `requires` token or a `boosts`
key was a real feature: a typo made the card silently unreachable, forever, with no error.
Both are checked here, which is the whole reason to generate rather than parse.
"""
from embed.common import BANNER, OUT, cpp_double, cpp_string, load

ARCHETYPES = "hero-archetypes.json"
OVERLAYS = "hero-axis-overlays.json"
ALIGNMENT = "hero-alignment.json"
NAMES = "hero-names.json"
WELLS = "hero-wells.json"
TABLES = "hero-code-tables.json"

# Exactly the reference's CARD_FIELDS. Compared for set equality, which is what makes a
# missing key a load failure rather than a default.
CARD_FIELDS = frozenset(("id", "name", "prior", "requires", "boosts", "drift", "intensity",
                         "core", "pole_notes", "lines", "faces"))
CORE_FIELDS = frozenset(("belief", "wants", "fears", "tells", "mechanic", "never"))
POLE_NOTES = ("lawful", "chaotic", "good", "evil")
OVERLAY_FIELDS = frozenset(("voice", "lies", "never", "cornered", "toward_player"))
INTENSITY_ENUM = {None: "HeroIntensity::None", "corner": "HeroIntensity::Corner",
                  "good_axis": "HeroIntensity::GoodAxis"}
FACE_ENUM = {None: "HeroFaceSpec::Single",
             ("public", "true"): "HeroFaceSpec::PublicTrue",
             ("calm", "unleashed"): "HeroFaceSpec::CalmUnleashed",
             ("first", "second"): "HeroFaceSpec::FirstSecond"}

# The 15 roles the wells actually emit. `policy['epithets'][role]` is a bare index, so a
# role with no list is a KeyError at generation time on a real world, not a fallback.
EMITTED_ROLES = ("pretender", "warlord", "magister", "domain_holder", "sovereign", "council",
                 "champion", "prophet", "heresiarch", "exile", "founder", "reeve", "castellan",
                 "harbourmaster", "keeper")


def load_hero_policies():
    cards_doc = load(ARCHETYPES, 1)
    overlays = load(OVERLAYS, 1)
    alignment = load(ALIGNMENT, 1)
    names = load(NAMES, 1)
    wells = load(WELLS, 1)
    tables = load(TABLES, 1)

    features = frozenset(tables["features"])
    if len(features) != 82:
        raise SystemExit("the archetype feature vocabulary is %d tokens, expected 82" % len(features))

    cards = cards_doc["cards"]
    if not cards:
        raise SystemExit("hero-archetypes.json has no cards; every person would fail to archetype")
    ids = [c["id"] for c in cards]
    if len(set(ids)) != len(ids):
        raise SystemExit("hero-archetypes.json has a duplicate card id")
    for card in cards:
        where = "archetype card " + card.get("id", "<unnamed>")
        if set(card) != CARD_FIELDS:
            raise SystemExit("%s key set is %s, expected %s"
                             % (where, sorted(card), sorted(CARD_FIELDS)))
        if set(card["prior"]) != {"law", "good"}:
            raise SystemExit("%s prior must be exactly {law, good}" % where)
        if set(card["core"]) != CORE_FIELDS:
            raise SystemExit("%s core key set is %s" % (where, sorted(card["core"])))
        for key in CORE_FIELDS - {"never"}:
            if not card["core"][key]:
                raise SystemExit("%s core.%s is empty" % (where, key))
        if not isinstance(card["core"]["never"], list):
            raise SystemExit("%s core.never must be a list" % where)
        if set(card["pole_notes"]) != set(POLE_NOTES) or not all(card["pole_notes"].values()):
            raise SystemExit("%s needs all four non-empty pole notes" % where)
        if not card["lines"] or any(len(k) != 2 for k in card["lines"]):
            raise SystemExit("%s lines must be non-empty and keyed by a two-character code" % where)
        if not card["requires"] or any(not conj for conj in card["requires"]):
            raise SystemExit("%s requires must be a non-empty list of non-empty conjunctions" % where)
        if card["intensity"] not in INTENSITY_ENUM:
            raise SystemExit("%s has an unknown intensity %r" % (where, card["intensity"]))
        spec = card["faces"] if card["faces"] is None else tuple(card["faces"])
        if spec not in FACE_ENUM:
            raise SystemExit("%s has an unknown faces spec %r" % (where, card["faces"]))
        if card["drift"] is not None and set(card["drift"]) != {"law", "good"}:
            raise SystemExit("%s drift must be null or exactly {law, good}" % where)
        # NOT in the reference. A precondition token outside the vocabulary can never be
        # satisfied and a boost key outside it can never apply, so either typo retires a
        # card -- or a third of its weight -- silently and permanently.
        for conj in card["requires"]:
            for token in conj:
                if token not in features:
                    raise SystemExit("%s requires %r, which is not a hero feature; the card "
                                     "would be unreachable forever" % (where, token))
        for token, factor in card["boosts"].items():
            if token not in features:
                raise SystemExit("%s boosts %r, which is not a hero feature; the multiplier "
                                 "could never apply" % (where, token))
            if not factor > 0:
                raise SystemExit("%s boosts %r by %r; a non-positive factor would zero or "
                                 "invert the weight" % (where, token, factor))

    for axis, poles in (("law", ("lawful", "neutral", "chaotic")), ("good", ("good", "neutral", "evil"))):
        if set(overlays[axis]) != set(poles):
            raise SystemExit("axis overlay %s must carry exactly %s" % (axis, sorted(poles)))
        for pole in poles:
            entry = overlays[axis][pole]
            if set(entry) != OVERLAY_FIELDS:
                raise SystemExit("overlay %s.%s key set is %s" % (axis, pole, sorted(entry)))
            if not isinstance(entry["never"], list):
                raise SystemExit("overlay %s.%s never must be a list" % (axis, pole))

    for role in EMITTED_ROLES:
        if not names["epithets"].get(role):
            raise SystemExit("no epithet list for role %r, which the wells emit; the namer "
                             "indexes it directly" % role)
    if not names["cover_roles"].get("default"):
        raise SystemExit("cover_roles.default is the required fallback for a role with no covers")

    for key in ("heir", "warlord", "dread", "domain_holder", "magister", "college_magister",
                "camp", "sovereign", "council", "diaspora", "champion", "reeve", "castellan",
                "harbourmaster", "keeper", "cult"):
        if key not in wells["precipitation"]:
            raise SystemExit("wells policy lost the %r precipitation rule" % key)
    if not 0.0 < wells["cap"] <= 1.0:
        raise SystemExit("the precipitation cap is a probability ceiling")
    # The gap below certainty is what keeps "history could have produced this and did not"
    # possible everywhere; a cap of exactly 1 retires the ledger's whole point.
    if wells["cap"] >= 1.0:
        raise SystemExit("a cap of 1.0 makes the roll ledger unable to record a near miss")
    for name, pair in list(alignment["civilization_bias"].items()) + list(alignment["deed_shifts"].items()) \
            + list(alignment["dread_family"].items()) + [("dread_base", alignment["dread_base"])]:
        if set(pair) != {"law", "good"}:
            raise SystemExit("alignment entry %r must be exactly {law, good}" % name)

    tiers = tables["fame"]["tiers"]
    # A DESCENDING ladder walked top-down. Reordering it mislabels every hero in the world,
    # and the failure is invisible because every value still gets a legal-looking band.
    if [t[0] for t in tiers] != sorted((t[0] for t in tiers), reverse=True):
        raise SystemExit("fame tiers must descend; the ladder is walked top-down")
    if tiers[-1][0] != 0.0:
        raise SystemExit("the last fame tier must floor at 0.0 or a fameless person has no band")
    if len(tables["quest_verbs"]) != len(set(tables["quest_verbs"])):
        raise SystemExit("the quest verb vocabulary has a duplicate")
    return cards_doc, overlays, alignment, names, wells, tables


def axis_pair(pair):
    return "{%s, %s}" % (cpp_double(pair["law"]), cpp_double(pair["good"]))


def pool_span(pool, values, array):
    """Append `values` to `pool` and return the span literal naming their slice."""
    begin = len(pool)
    pool.extend(values)
    return "{%s + %d, %d}" % (array, begin, len(values))


def chunked(indent, items, per_line=4):
    return [indent + " ".join(items[i:i + per_line]) for i in range(0, len(items), per_line)]


def render_heroes():
    cards_doc, overlays, alignment, names, wells, tables = load_hero_policies()
    cards = cards_doc["cards"]

    tokens, clauses, boosts, nevers, lines = [], [], [], [], []
    card_rows = []
    for card in cards:
        clause_begin = len(clauses)
        for conj in card["requires"]:
            # Doubled braces: HeroClause has one member and it is itself a span.
            clauses.append("{%s}" % pool_span(tokens, conj, "kReqTokens"))
        boost_begin = len(boosts)
        for feature, factor in card["boosts"].items():
            boosts.append("{%s, %s}" % (cpp_string(feature), cpp_double(factor)))
        never_span = pool_span(nevers, card["core"]["never"], "kNever")
        line_begin = len(lines)
        for code, text in card["lines"].items():
            lines.append("{%s, %s}" % (cpp_string(code), cpp_string(text)))
        spec = card["faces"] if card["faces"] is None else tuple(card["faces"])
        card_rows.append(dict(
            card=card,
            clauses="{kClauses + %d, %d}" % (clause_begin, len(clauses) - clause_begin),
            boosts="{kBoosts + %d, %d}" % (boost_begin, len(boosts) - boost_begin),
            never=never_span,
            lines="{kLines + %d, %d}" % (line_begin, len(lines) - line_begin),
            intensity=INTENSITY_ENUM[card["intensity"]],
            faces=FACE_ENUM[spec]))

    overlay_never = []
    overlay_rows = {}
    for axis, poles in (("law", ("chaotic", "neutral", "lawful")), ("good", ("evil", "neutral", "good"))):
        rows = []
        for pole in poles:
            e = overlays[axis][pole]
            rows.append("    {%s, %s, %s,\n     %s,\n     %s},  // %s"
                        % (cpp_string(e["voice"]), cpp_string(e["lies"]), cpp_string(e["cornered"]),
                           cpp_string(e["toward_player"]),
                           pool_span(overlay_never, e["never"], "kOverlayNever"), pole))
        overlay_rows[axis] = rows

    epithet_pool, epithet_rows = [], []
    for role, entries in names["epithets"].items():
        epithet_rows.append("    {%s, %s},"
                            % (cpp_string(role), pool_span(epithet_pool, entries, "kEpithetPool")))
    cover_pool, cover_rows = [], []
    for role, entries in names["cover_roles"].items():
        cover_rows.append("    {%s, %s},"
                          % (cpp_string(role), pool_span(cover_pool, entries, "kCoverPool")))

    p = wells["precipitation"]
    q = wells["quests"]
    fame = tables["fame"]
    tiers = {name: floor for floor, name in fame["tiers"]}

    lines_out = [BANNER.format(source="hero-archetypes.json, hero-axis-overlays.json, "
                                      "hero-alignment.json, hero-names.json, hero-wells.json "
                                      "and hero-code-tables.json")]
    lines_out.append('#include "fwg/heroes.hpp"\n')
    lines_out.append("namespace fwg {")
    lines_out.append("namespace {\n")

    lines_out.append("// Precondition tokens, flattened. A card's `requires` is a span of CLAUSES and each")
    lines_out.append("// clause is a span of these; a card is eligible when ANY clause holds entirely.")
    lines_out.append("constexpr std::string_view kReqTokens[] = {")
    lines_out.extend(chunked("    ", ["%s," % cpp_string(t) for t in tokens], 4))
    lines_out.append("};\n")
    lines_out.append("constexpr HeroClause kClauses[] = {")
    lines_out.extend(chunked("    ", ["%s," % c for c in clauses], 3))
    lines_out.append("};\n")

    lines_out.append("// FILE ORDER. The weight is a float product over these terms and multiplication")
    lines_out.append("// does not associate, so a reordered table can move a card's last bit.")
    lines_out.append("constexpr HeroBoost kBoosts[] = {")
    lines_out.extend(chunked("    ", ["%s," % b for b in boosts], 2))
    lines_out.append("};\n")

    lines_out.append("// The hard lines a card's own core declares. ORDER SURVIVES INTO THE EXPORT: the")
    lines_out.append("// persona's never-list starts here and appends the two overlays, first-wins.")
    lines_out.append("constexpr std::string_view kNever[] = {")
    lines_out.extend("    %s," % cpp_string(n) for n in nevers)
    lines_out.append("};\n")

    lines_out.append("// FILE ORDER IS A CONTRACT. A person whose two-character code is absent from a")
    lines_out.append("// card reads the first line whose GOOD letter matches, and failing that the FIRST")
    lines_out.append("// ENTRY HERE -- which is what 27 of the 32 cards give every neutral-good person.")
    lines_out.append("constexpr HeroLine kLines[] = {")
    lines_out.extend("    %s," % line for line in lines)
    lines_out.append("};\n")

    lines_out.append("// ORDER IS A CONTRACT: the eligible cards are collected in this order and walked")
    lines_out.append("// with a subtractive cumulative selector, so a card's position is observable in")
    lines_out.append("// the chosen card for a given draw. Nothing sorts first.")
    lines_out.append("constexpr HeroArchetypeCard kCards[] = {")
    for row in card_rows:
        c = row["card"]
        core = c["core"]
        pn = c["pole_notes"]
        lines_out.append("    {%s, %s, %s," % (cpp_string(c["id"]), cpp_string(c["name"]), axis_pair(c["prior"])))
        lines_out.append("     %s, %s," % (row["clauses"], row["boosts"]))
        lines_out.append("     %s, %s, %s,"
                         % ("true" if c["drift"] else "false",
                            axis_pair(c["drift"]) if c["drift"] else "{0.0, 0.0}",
                            row["intensity"]))
        lines_out.append("     {%s, %s," % (cpp_string(core["belief"]), cpp_string(core["wants"])))
        lines_out.append("      %s, %s," % (cpp_string(core["fears"]), cpp_string(core["tells"])))
        lines_out.append("      %s, %s}," % (cpp_string(core["mechanic"]), row["never"]))
        lines_out.append("     {%s," % cpp_string(pn["lawful"]))
        lines_out.append("      %s," % cpp_string(pn["chaotic"]))
        lines_out.append("      %s," % cpp_string(pn["good"]))
        lines_out.append("      %s}," % cpp_string(pn["evil"]))
        lines_out.append("     %s, %s}," % (row["lines"], row["faces"]))
    lines_out.append("};\n")

    lines_out.append("constexpr std::string_view kOverlayNever[] = {")
    lines_out.extend("    %s," % cpp_string(n) for n in overlay_never)
    lines_out.append("};\n")
    lines_out.append("// Indexed by pole + 1: 0 is chaotic/evil, 1 neutral, 2 lawful/good.")
    lines_out.append("constexpr HeroOverlay kLawOverlays[] = {")
    lines_out.extend(overlay_rows["law"])
    lines_out.append("};\n")
    lines_out.append("constexpr HeroOverlay kGoodOverlays[] = {")
    lines_out.extend(overlay_rows["good"])
    lines_out.append("};\n")

    lines_out.append("// POSITIONALLY DRAWN, both of them. The epithet filter preserves relative order,")
    lines_out.append("// so every surviving subsequence is a contract too.")
    lines_out.append("constexpr std::string_view kEpithetPool[] = {")
    lines_out.extend("    %s," % cpp_string(e) for e in epithet_pool)
    lines_out.append("};\n")
    lines_out.append("constexpr HeroNameList kEpithets[] = {")
    lines_out.extend(epithet_rows)
    lines_out.append("};\n")
    lines_out.append("constexpr std::string_view kCoverPool[] = {%s};"
                     % ", ".join(cpp_string(c) for c in cover_pool))
    lines_out.append("constexpr HeroNameList kCovers[] = {")
    lines_out.extend(cover_rows)
    lines_out.append("};\n")

    lines_out.append("constexpr HeroWellsPolicy kWells = {")
    lines_out.append("    %s," % cpp_double(wells["cap"]))
    lines_out.append("    %s, {%s, %s, %s},"
                     % (cpp_double(p["heir"]["base"]),
                        cpp_double(p["heir"]["per_city_class"]["small"]),
                        cpp_double(p["heir"]["per_city_class"]["medium"]),
                        cpp_double(p["heir"]["per_city_class"]["capital"])))
    lines_out.append("    %s, %s, %s, %s, %s, %s,"
                     % (cpp_double(p["warlord"]["base"]),
                        cpp_double(p["warlord"]["per_war_kind"]["civil"]),
                        cpp_double(p["warlord"]["per_war_kind"]["regional"]),
                        cpp_double(p["warlord"]["per_war_kind"]["international"]),
                        cpp_double(p["warlord"]["per_extra_war"]),
                        "true" if p["warlord"]["one_per_city"] else "false"))
    lines_out.append("    %s, %s," % (cpp_double(p["dread"]["base"]), cpp_double(p["dread"]["per_nest_tier"])))
    lines_out.append("    %s, %s," % (cpp_double(p["domain_holder"]["base"]),
                                      cpp_double(p["domain_holder"]["per_intensity"])))
    lines_out.append("    %s, %s," % (cpp_double(p["magister"]["base"]),
                                      cpp_double(p["college_magister"]["base"])))
    lines_out.append("    %s, {%s, %s, %s},"
                     % (cpp_double(p["camp"]["base"]),
                        cpp_double(p["camp"]["per_city_class"]["small"]),
                        cpp_double(p["camp"]["per_city_class"]["medium"]),
                        cpp_double(p["camp"]["per_city_class"]["capital"])))
    lines_out.append("    %s," % cpp_double(p["sovereign"]["base"]))
    lines_out.append("    %s, %s," % (cpp_double(p["council"]["base"]), cpp_double(p["council"]["capital_bonus"])))
    lines_out.append("    %s," % cpp_double(p["diaspora"]["base"]))
    lines_out.append("    %s, %s, %s," % (cpp_double(p["champion"]["base"]),
                                          cpp_double(p["champion"]["per_nest_tier"]),
                                          cpp_double(p["champion"]["reach_factor"])))
    lines_out.append("    %s, %s, %s, %s," % (cpp_double(p["reeve"]["base"]),
                                              cpp_double(p["reeve"]["starving_bonus"]),
                                              cpp_double(p["reeve"]["threatened_bonus"]),
                                              cpp_double(p["reeve"]["reach_factor"])))
    lines_out.append("    %s, %s, %s," % (cpp_double(p["castellan"]["base"]),
                                          cpp_double(p["castellan"]["pressed_bonus"]),
                                          cpp_double(p["castellan"]["per_defence"])))
    lines_out.append("    %s, %s, %s," % (cpp_double(p["harbourmaster"]["base"]),
                                          cpp_double(p["harbourmaster"]["per_route"]),
                                          cpp_double(p["harbourmaster"]["terminal_bonus"])))
    lines_out.append("    %s, %s," % (cpp_double(p["keeper"]["base"]), cpp_double(p["cult"]["base"])))
    lines_out.append("    %s," % cpp_double(wells["magic"]["hook_intensity_delta"]))
    lines_out.append("    %s, %s," % (cpp_string(q["ley_verb"]["strengthen"]),
                                      cpp_string(q["ley_verb"]["weaken"])))
    lines_out.append("    %s, %s," % (cpp_double(q["ley_floor"]), cpp_double(q["ley_intensity_weight"])))
    lines_out.append("    %s, %s," % (cpp_double(q["pressure_per_tier"]), cpp_double(q["pressure_cap"])))
    lines_out.append("    %d, %d," % (q["difficulty_min"], q["difficulty_max"]))
    lines_out.append("    %s, %s," % (cpp_double(wells["features"]["tainted_threshold"]),
                                      "true" if wells["placement"]["heir_leads_own_camp"] else "false"))
    lines_out.append("};\n")

    seat_pool, seat_rows = [], []
    for city_class, seats in wells["seats"].items():
        seat_rows.append("    {%s, %s}," % (cpp_string(city_class), pool_span(seat_pool, seats, "kSeatPool")))
    lines_out.append("// LIST ORDER IS THE CANDIDATE ORDER, therefore the roll-ledger order and the")
    lines_out.append("// council org's members order. Both are exported unsorted.")
    lines_out.append("constexpr std::string_view kSeatPool[] = {%s};"
                     % ", ".join(cpp_string(s) for s in seat_pool))
    lines_out.append("constexpr HeroNameList kSeats[] = {")
    lines_out.extend(seat_rows)
    lines_out.append("};\n")

    lines_out.append("// NOTE `weave` is here AND is the default school for a magister whose ruin records")
    lines_out.append("// none, so a magister with no recorded school is dark by default. That is the gate")
    lines_out.append("// on the Deceiver card and on the Temptation trope.")
    lines_out.append("constexpr std::string_view kDarkSchools[] = {%s};"
                     % ", ".join(cpp_string(s) for s in wells["magic"]["dark_schools"]))
    lines_out.append("constexpr std::string_view kMagisterAtCamp[] = {%s};"
                     % ", ".join(cpp_string(s) for s in wells["placement"]["magister_at_camp"]))
    lines_out.append("")
    lines_out.append("struct TextPair { std::string_view key, value; };")
    lines_out.append("struct NumberRow { std::string_view key; double value; };")
    lines_out.append("struct PairRow { std::string_view key; HeroAxisPair value; };\n")
    lines_out.append("constexpr TextPair kOpposites[] = {%s};"
                     % ", ".join("{%s, %s}" % (cpp_string(a), cpp_string(b))
                                 for a, b in wells["magic"]["opposites"].items()))
    lines_out.append("constexpr NumberRow kOpposition[] = {%s};"
                     % ", ".join("{%s, %s}" % (cpp_string(k), cpp_double(v))
                                 for k, v in q["opposition"].items()))
    lines_out.append("")

    lines_out.append("constexpr HeroAlignmentPolicy kAlignment = {%s, %s};"
                     % (cpp_double(alignment["jitter"]), axis_pair(alignment["dread_base"])))
    lines_out.append("// ABSENCE IS EXACTLY AN AUTHORED {0, 0} in all three: the shift is added, never")
    lines_out.append("// multiplied, so an unlisted people simply centres at the origin.")
    for name, table in (("kCivBias", alignment["civilization_bias"]),
                        ("kDeedShifts", alignment["deed_shifts"]),
                        ("kDreadFamily", alignment["dread_family"])):
        lines_out.append("constexpr PairRow %s[] = {" % name)
        lines_out.extend("    {%s, %s}," % (cpp_string(k), axis_pair(v)) for k, v in table.items())
        lines_out.append("};")
    lines_out.append("")

    lines_out.append("// The 82 tokens a card may name. The story web declares the same list and a test")
    lines_out.append("// ties the two together; the reference held two literal copies in sync by comment.")
    lines_out.append("constexpr std::string_view kFeatures[] = {")
    lines_out.extend(chunked("    ", ["%s," % cpp_string(f) for f in tables["features"]], 3))
    lines_out.append("};\n")
    lines_out.append("constexpr std::string_view kVerbs[] = {%s};"
                     % ", ".join(cpp_string(v) for v in tables["quest_verbs"]))
    lines_out.append("constexpr std::string_view kUnsited[] = {%s};"
                     % ", ".join(cpp_string(v) for v in tables["unsited_reasons"]))
    lines_out.append("")

    cw, ww, sw = fame["class_weight"], fame["war_weight"], fame["site_weight"]
    lines_out.append("constexpr HeroFameTuning kFame = {%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s};"
                     % (cpp_double(cw["small"]), cpp_double(cw["medium"]), cpp_double(cw["capital"]),
                        cpp_double(ww["civil"]), cpp_double(ww["regional"]), cpp_double(ww["international"]),
                        cpp_double(sw["hamlet"]), cpp_double(sw["fortress"]), cpp_double(sw["port"]),
                        cpp_double(sw["shrine"]), cpp_double(fame["legend_reach"]),
                        cpp_double(tiers["legendary"]), cpp_double(tiers["renowned"])))
    lines_out.append("// `librarian` is in the table and UNREACHABLE: no city class declares that seat.")
    lines_out.append("constexpr NumberRow kSeatWeights[] = {%s};"
                     % ", ".join("{%s, %s}" % (cpp_string(k), cpp_double(v))
                                 for k, v in fame["seat_weight"].items()))
    lines_out.append("")

    def text_pairs(name, table):
        lines_out.append("constexpr HeroTextPair %s[] = {%s};"
                         % (name, ", ".join("{%s, %s}" % (cpp_string(k), cpp_string(v))
                                            for k, v in table.items())))

    def text_list(name, values):
        lines_out.append("constexpr std::string_view %s[] = {%s};"
                         % (name, ", ".join(cpp_string(v) for v in values)))

    lines_out.append("// Authored English and authored classifications: content, not mechanism. Every one")
    lines_out.append("// of these reaches a player through a log line, a situation sentence or a name.")
    text_pairs("kClassWord", tables["class_word"])
    text_pairs("kCausePhrases", tables["cause_phrases"])
    text_pairs("kCompanionJoin", tables["companion_join"])
    text_pairs("kDiasporaRoles", tables["diaspora_roles"])
    text_pairs("kSeatTitles", tables["seat_titles"])
    text_pairs("kRoleWord", tables["role_word"])
    text_pairs("kSiteRoles", tables["site_roles"])
    lines_out.append("constexpr HeroRelicRule kRelicByCause[] = {%s};"
                     % ", ".join("{%s, %s, %s}" % (cpp_string(k), cpp_string(v[0]), cpp_string(v[1]))
                                 for k, v in tables["relic_by_cause"].items()))
    text_list("kBeastCauses", tables["beast_causes"])
    text_list("kLongLivedFamilies", tables["long_lived_families"])
    text_list("kDarkFamilies", tables["dark_families"])
    text_list("kSeatedRoles", tables["seated"])
    text_list("kTaintingLayers", tables["tainting_layers"])
    text_list("kSeatExemptRoles", tables["seat_exempt_roles"])
    text_list("kSchoolScoringExcludes", tables["school_scoring_excludes"])
    text_list("kPressureExempt", tables["pressure_exempt"])
    lines_out.append("")
    lines_out.append("constexpr HeroWordTables kWords = {")
    lines_out.append("    kClassWord, kCausePhrases, kCompanionJoin, kDiasporaRoles,")
    lines_out.append("    kSeatTitles, %s, kRoleWord, %s, kSiteRoles, kRelicByCause,"
                     % (cpp_string(tables["seat_title_default"]), cpp_string(tables["role_word_default"])))
    lines_out.append("    kBeastCauses, kLongLivedFamilies, kDarkFamilies, kSeatedRoles,")
    lines_out.append("    kTaintingLayers, kSeatExemptRoles, kSchoolScoringExcludes, kPressureExempt,")
    lines_out.append("    %s, %s, %s," % (cpp_string(tables["default_school"]),
                                          cpp_string(tables["self_magic_cause"]),
                                          cpp_string(tables["deed_keeper"])))
    lines_out.append("};\n")

    src = cards_doc["source_identity"]
    digest_source = "".join(sorted(
        str(load(name, 1)) for name in (ARCHETYPES, OVERLAYS, ALIGNMENT, NAMES, WELLS, TABLES)))
    from embed.common import sha256_of
    lines_out.append("constexpr std::string_view kMethod =")
    lines_out.append("    %s;" % cpp_string(tables["method"]))
    lines_out.append("constexpr std::string_view kLimits =")
    lines_out.append("    %s;" % cpp_string(tables["limits"]))
    lines_out.append("")
    lines_out.append("}  // namespace\n")

    lines_out.append("std::span<const HeroArchetypeCard> hero_archetype_cards() { return kCards; }\n")
    lines_out.append("""const HeroArchetypeCard* hero_archetype_card(std::string_view id) {
    for (const HeroArchetypeCard& card : kCards) {
        if (card.id == id) return &card;
    }
    return nullptr;
}
""")
    lines_out.append("std::span<const HeroOverlay> hero_law_overlays() { return kLawOverlays; }")
    lines_out.append("std::span<const HeroOverlay> hero_good_overlays() { return kGoodOverlays; }\n")
    lines_out.append("std::span<const HeroNameList> hero_epithets() { return kEpithets; }")
    lines_out.append("std::span<const HeroNameList> hero_cover_roles() { return kCovers; }\n")
    lines_out.append("""namespace {
std::span<const std::string_view> lookup(std::span<const HeroNameList> table, std::string_view key) {
    for (const HeroNameList& row : table) {
        if (row.role == key) return row.entries;
    }
    return {};
}
}  // namespace

std::span<const std::string_view> hero_epithets_for(std::string_view role) {
    return lookup(kEpithets, role);
}

std::span<const std::string_view> hero_covers_for(std::string_view role) {
    return lookup(kCovers, role);
}

std::span<const std::string_view> hero_seats_for(std::string_view city_class) {
    return lookup(kSeats, city_class);
}
""")
    lines_out.append("const HeroWellsPolicy& hero_wells_policy() { return kWells; }\n")
    lines_out.append("std::span<const std::string_view> hero_dark_schools() { return kDarkSchools; }")
    lines_out.append("std::span<const std::string_view> hero_magister_at_camp() { return kMagisterAtCamp; }\n")
    lines_out.append("""std::string_view hero_opposite_school(std::string_view school) {
    for (const TextPair& row : kOpposites) {
        if (row.key == school) return row.value;
    }
    return {};
}

double hero_opposition_for(std::string_view kind) {
    for (const NumberRow& row : kOpposition) {
        if (row.key == kind) return row.value;
    }
    // An unlisted kind prices at the floor, NOT at zero: a new hook kind silently costs 1.
    return static_cast<double>(kWells.difficulty_min);
}
""")
    lines_out.append("const HeroAlignmentPolicy& hero_alignment_policy() { return kAlignment; }\n")
    lines_out.append("""namespace {
HeroAxisPair pair_or_zero(std::span<const PairRow> table, std::string_view key) {
    for (const PairRow& row : table) {
        if (row.key == key) return row.value;
    }
    return {0.0, 0.0};
}
}  // namespace

HeroAxisPair hero_civilization_bias(std::string_view civilization_id) {
    return pair_or_zero(kCivBias, civilization_id);
}

HeroAxisPair hero_deed_shift(std::string_view key) { return pair_or_zero(kDeedShifts, key); }

HeroAxisPair hero_dread_family_shift(std::string_view family) {
    return pair_or_zero(kDreadFamily, family);
}
""")
    lines_out.append("std::span<const std::string_view> hero_feature_vocabulary() { return kFeatures; }")
    lines_out.append("std::span<const std::string_view> hero_quest_verbs() { return kVerbs; }")
    lines_out.append("std::span<const std::string_view> hero_unsited_reasons() { return kUnsited; }\n")
    lines_out.append("const HeroFameTuning& hero_fame_tuning() { return kFame; }\n")
    lines_out.append("""double hero_seat_weight(std::string_view seat) {
    for (const NumberRow& row : kSeatWeights) {
        if (row.key == seat) return row.value;
    }
    return 1.0;
}
""")
    lines_out.append("""HeroCatalogueIdentity hero_catalogue_identity() {
    return {%d, %d, %d, %d, %d, %d, %s};
}
""" % (1, cards_doc["source_identity"]["revision"], overlays["source_identity"]["revision"],
       alignment["source_identity"]["revision"], names["source_identity"]["revision"],
       wells["source_identity"]["revision"], cpp_string(sha256_of(digest_source))))
    lines_out.append("const HeroWordTables& hero_word_tables() { return kWords; }\n")
    lines_out.append("""std::string_view hero_word(std::span<const HeroTextPair> table, std::string_view key,
                           std::string_view fallback) {
    for (const HeroTextPair& row : table) {
        if (row.key == key) return row.value;
    }
    return fallback;
}
""")
    lines_out.append("std::string_view heroes_method() { return kMethod; }")
    lines_out.append("std::string_view heroes_limits() { return kLimits; }\n")
    lines_out.append("}  // namespace fwg")
    assert src  # the identity block is what `policy_revision` republishes
    return "\n".join(lines_out) + "\n"


TARGETS = {OUT / "content_heroes.cpp": render_heroes}
