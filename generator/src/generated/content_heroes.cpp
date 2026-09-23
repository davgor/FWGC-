// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/hero-archetypes.json, hero-axis-overlays.json, hero-alignment.json, hero-names.json, hero-wells.json and hero-code-tables.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/heroes.hpp"

namespace fwg {
namespace {

// Precondition tokens, flattened. A card's `requires` is a span of CLAUSES and each
// clause is a span of these; a card is eligible when ANY clause holds entirely.
constexpr std::string_view kReqTokens[] = {
    "role:warlord", "role:sovereign", "role:founder", "role:pretender",
    "home:fell", "role:exile", "order:member", "role:domain_holder",
    "keypoint:ruin_born", "role:council", "role:keeper", "legend:predecessor",
    "deed:credited_not_actual", "villain:prior_age", "stake:regained", "ley:tainted_home",
    "order:member", "order:against_dark", "rival:spared", "role:council",
    "seat:chaplain", "role:warlord", "war:civil_victor", "role:sovereign",
    "realm:cold_conflicts_2", "role:schemer", "stakes:2", "role:council",
    "seat:mage", "seat:trade", "role:council", "seat:magistrate",
    "role:champion", "deed:dark_nest", "role:dread", "dread:person_shaped",
    "fame:renowned", "home:fell", "civ:lost_many", "home:fell",
    "ally:lost", "deeds:two_civilizations", "deeds:two_schools", "fame:legendary",
    "role:magister", "school:dark", "role:domain_holder", "school:dark",
    "role:schemer", "mentor:successor", "role:magister", "seat:college",
    "role:council", "seat:mage", "role:council", "seat:librarian",
    "role:camp_leader", "role:schemer", "stake:none", "role:council",
    "seat:magistrate", "role:prophet", "role:heresiarch", "role:council",
    "seat:chaplain", "role:keeper", "shrine:ruin", "role:pretender",
    "heir:small", "heir:unclaimed", "role:reeve", "role:champion",
    "defence:failed", "role:warlord", "home:fell_later", "council:outranks_sovereign",
    "role:council", "seat:commander", "role:council", "seat:magistrate",
    "role:schemer", "sovereign:advised", "role:sovereign", "dynasty:founding",
    "realm:contested", "realm:tyrant", "seat:none", "role:champion",
    "order:remnant", "seat:trade", "routes:2", "role:magister",
    "college:destroyed", "role:pretender", "dread:living", "role:champion",
    "dread:failed_against", "role:reeve", "hamlet:threatened", "sides:both",
    "role:domain_holder", "seat:none", "order:member", "warden:post",
    "role:champion", "role:dread", "dread:long_lived", "dread:ages_2",
    "role:dread", "dread:long_lived", "fame:renowned",
};

constexpr HeroClause kClauses[] = {
    {{kReqTokens + 0, 1}}, {{kReqTokens + 1, 1}}, {{kReqTokens + 2, 1}},
    {{kReqTokens + 3, 2}}, {{kReqTokens + 5, 1}}, {{kReqTokens + 6, 1}},
    {{kReqTokens + 7, 2}}, {{kReqTokens + 9, 1}}, {{kReqTokens + 10, 1}},
    {{kReqTokens + 11, 1}}, {{kReqTokens + 12, 1}}, {{kReqTokens + 13, 2}},
    {{kReqTokens + 15, 1}}, {{kReqTokens + 16, 2}}, {{kReqTokens + 18, 1}},
    {{kReqTokens + 19, 2}}, {{kReqTokens + 21, 2}}, {{kReqTokens + 23, 2}},
    {{kReqTokens + 25, 2}}, {{kReqTokens + 27, 2}}, {{kReqTokens + 29, 1}},
    {{kReqTokens + 30, 2}}, {{kReqTokens + 32, 2}}, {{kReqTokens + 34, 2}},
    {{kReqTokens + 36, 1}}, {{kReqTokens + 37, 2}}, {{kReqTokens + 39, 2}},
    {{kReqTokens + 41, 1}}, {{kReqTokens + 42, 1}}, {{kReqTokens + 43, 1}},
    {{kReqTokens + 44, 2}}, {{kReqTokens + 46, 2}}, {{kReqTokens + 48, 1}},
    {{kReqTokens + 49, 1}}, {{kReqTokens + 50, 2}}, {{kReqTokens + 52, 2}},
    {{kReqTokens + 54, 2}}, {{kReqTokens + 56, 1}}, {{kReqTokens + 57, 2}},
    {{kReqTokens + 59, 2}}, {{kReqTokens + 61, 1}}, {{kReqTokens + 62, 1}},
    {{kReqTokens + 63, 2}}, {{kReqTokens + 65, 2}}, {{kReqTokens + 67, 2}},
    {{kReqTokens + 69, 1}}, {{kReqTokens + 70, 1}}, {{kReqTokens + 71, 2}},
    {{kReqTokens + 73, 2}}, {{kReqTokens + 75, 1}}, {{kReqTokens + 76, 2}},
    {{kReqTokens + 78, 3}}, {{kReqTokens + 81, 1}}, {{kReqTokens + 82, 3}},
    {{kReqTokens + 85, 2}}, {{kReqTokens + 87, 2}}, {{kReqTokens + 89, 2}},
    {{kReqTokens + 91, 2}}, {{kReqTokens + 93, 2}}, {{kReqTokens + 95, 2}},
    {{kReqTokens + 97, 2}}, {{kReqTokens + 99, 1}}, {{kReqTokens + 100, 2}},
    {{kReqTokens + 102, 2}}, {{kReqTokens + 104, 1}}, {{kReqTokens + 105, 3}},
    {{kReqTokens + 108, 3}},
};

// FILE ORDER. The weight is a float product over these terms and multiplication
// does not associate, so a reordered table can move a card's last bit.
constexpr HeroBoost kBoosts[] = {
    {"realm:contested", 3.0}, {"war:international_victor", 1.5},
    {"role:founder", 0.5}, {"role:sovereign", 1.5},
    {"heir:capital", 1.3}, {"role:council", 0.5},
    {"role:pretender", 1.5}, {"civ:lost_many", 1.5},
    {"fame:legendary", 1.0}, {"heir:capital", 1.5},
    {"dread:ages_2", 2.0},
};

// The hard lines a card's own core declares. ORDER SURVIVES INTO THE EXPORT: the
// persona's never-list starts here and appends the two overlays, first-wins.
constexpr std::string_view kNever[] = {
    "accept a peace that leaves a rival crowned",
    "sell out the new home to buy back the old",
    "reinterpret the oath to escape it",
    "alter the target of the inherited claim",
    "confess unprompted",
    "return to the old cause for gain",
    "use the taint on an innocent",
    "kill a surrendered foe",
    "apologise in public",
    "share credit",
    "act without a fallback",
    "die for anything",
    "turn on their pack while calm",
    "act outside the corner, even to win",
    "return to ordinary life",
    "betray that face's cause while wearing it",
    "compromise the moral pole",
    "reveal the true face voluntarily",
    "fight the successor's fight for them",
    "side with the strongest",
    "tolerate blasphemy",
    "abandon people who depend on them",
    "admit the fall was their fault",
    "take the throne themselves",
    "contradict the adviser in public",
    "bow",
    "fight for pay alone",
    "destroy an asset out of spite",
    "give the dangerous half of the answer",
    "let the Dread go",
    "die for a losing side",
    "leave the post",
    "hurry",
};

// FILE ORDER IS A CONTRACT. A person whose two-character code is absent from a
// card reads the first line whose GOOD letter matches, and failing that the FIRST
// ENTRY HERE -- which is what 27 of the 32 cards give every neutral-good person.
constexpr HeroLine kLines[] = {
    {"LG", "You'll keep your laws, your gods and your granaries. You'll lose your war. That's the whole offer."},
    {"CE", "Kingdoms are kindling. I'm just the one honest enough to bring the torch."},
    {"LG", "Everything I have left is behind me now, and it needs a wall."},
    {"NE", "They exiled me for what I did. They should have asked what I'd do for the ones who followed me."},
    {"LN", "I would spare you if the vow allowed one word of mercy. It does not."},
    {"CE", "I swore to keep the pass closed. Nobody said how many bodies it takes."},
    {"NG", "This was his sword and his fight. The sword still cuts."},
    {"LE", "She spared them. That's the only part of her plan I'm changing."},
    {"CG", "You'd have done the same. Let's not bore these good people with technicalities."},
    {"LE", "The chronicle says I killed it. Chronicles are law here. Be careful who you contradict."},
    {"NG", "Don't thank me for this. Just make sure it was necessary."},
    {"LE", "I'm reformed. That's why I'll do it cleanly this time."},
    {"LG", "Stand back when it starts. I'll be me again in a moment. Usually."},
    {"CN", "It only comes out for the ones who earn it. Don't earn it."},
    {"CG", "You'll always get one more chance. That's the point of me."},
    {"LE", "Of course you're pardoned. Now you owe me, and everyone knows it."},
    {"CG", "I hanged three grain-hoarders and nobody starved this winter. Argue with the full bellies."},
    {"LE", "Kneel or don't. The outcome is the same. Kneeling is faster."},
    {"LG", "You did exactly what I hoped. I keep hope and plan separate."},
    {"CE", "There were four ways this ended. You picked the one where you're useful. Well done."},
    {"NG", "This one's at cost. The orphanage doesn't run on prayers."},
    {"NE", "I like you. So this one's at cost. Next time we talk about interest."},
    {"LG", "Go. Now. I'm not asking twice because I won't be here to ask."},
    {"CE", "Stay. Watch. It's better with an audience."},
    {"LG", "It isn't that I won't. There's no version of me that could."},
    {"CE", "There's no version of me that wouldn't."},
    {"NG", "Do you know what's left when the last thing burns? Room to carry someone else out."},
    {"CE", "Room to move."},
    {"NG", "I said that? … Which of us are you talking to now?"},
    {"LE", "Her. You were talking to her. I'm the one who keeps the promises."},
    {"LG", "I don't need you to believe me. Watch what happens next."},
    {"CE", "I don't need you to serve me. You will, but I don't need it."},
    {"NE", "You saved my life. Let me repay it. There's a shrine poisoning the valley; I know how to cleanse it."},
    {"CG", "I lied about why. I didn't lie about what it does. You'd have argued, and we didn't have the week."},
    {"NG", "I could tell you. Then it would be my victory."},
    {"LE", "I made you. Remember that when you think you've outgrown me."},
    {"CG", "Of course I lied. You'd have been so bored."},
    {"CE", "The punchline's tomorrow at dawn. Bring the whole court."},
    {"LG", "I've doubted. That's how I know doubt's the enemy. Sit, eat, I'll show you."},
    {"CE", "The fire doesn't ask whether you believe. Neither will I, after tonight."},
    {"NG", "Fine. But when this is done, someone else has the sword."},
    {"NE", "I didn't ask for the crown. I'm keeping it, though. Let's be clear about that."},
    {"LG", "They sang about me once. The song's true. That's the worst part."},
    {"CE", "Ask them how the song ends. I'll tell you: it doesn't. Nobody's left to sing it."},
    {"LG", "His Majesty will decide. I've helped him understand what he decides. It's usually the kind thing."},
    {"NE", "The queen reigns. Everyone who matters knows who signs."},
    {"NG", "My chancellor says it's wise. He's rarely wrong, I'm told."},
    {"LE", "Do as the chancellor says. And do it where I can watch."},
    {"CG", "We don't have an army. We have everyone he's stepped on."},
    {"LE", "After him, there's a vacancy. I've been very clear about who fills it."},
    {"LG", "The Wardens of the Pale number one, today. Tell me where you're going."},
    {"CE", "The order's dead. Its name isn't. I rent it out."},
    {"LN", "Wars end. Debts don't. Sit, eat. Let's talk about after."},
    {"NE", "Their granary's empty. Mine isn't. That's not a threat, it's a price list."},
    {"NG", "I know exactly what you're asking. That's why I'm saying no."},
    {"NE", "I know exactly what you're asking. It'll cost you exactly what it's worth."},
    {"NG", "You've seen its tracks? Show me. Then get your people out of its path."},
    {"CE", "Show me. Everything else can burn."},
    {"CG", "I was with them until I saw what they did to the village. Now I'm with you. Ask me again after the next village."},
    {"NE", "I was never really with them. I'm not really with you. Relax; it's the same thing."},
    {"LN", "Third night this month it's tested the ward. You want through? You're coming back with a message."},
    {"CE", "Nothing gets past me. That includes you, unless you've brought something it likes."},
    {"LN", "Your grandmother's grandmother asked the same. I gave her the same answer. Wait."},
    {"CE", "Your grandmother's grandmother asked the same. She was delicious."},
};

// ORDER IS A CONTRACT: the eligible cards are collected in this order and walked
// with a subtractive cumulative selector, so a card's position is observable in
// the chosen card for a given draw. Nothing sorts first.
constexpr HeroArchetypeCard kCards[] = {
    {"conqueror", "The Conqueror", {0.6, 0.0},
     {kClauses + 0, 3}, {kBoosts + 0, 4},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Many crowns mean endless war; I will end it with one.", "Everything under one rule, theirs.",
      "A peace made without them.", "Mourns the dead of their own wars; keeps maps, not trophies.",
      "Claim `unify` targets every neighbouring realm; refuses treaties that leave two sovereigns.", {kNever + 0, 1}},
     {"Conquest by treaty, marriage and siege in that order; every surrender is kept.",
      "Conquest by charisma and sudden strikes; borders redrawn on a whim.",
      "Liberates as it annexes; the conquered eat better than before.",
      "The one peace is their peace; dissent is a border to close."},
     {kLines + 0, 2}, HeroFaceSpec::Single},
    {"exiled_hero", "The Exiled Hero", {0.0, 0.6},
     {kClauses + 3, 2}, {kBoosts + 4, 1},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Home is the people, not the ground.", "Their people safe wherever they now stand.",
      "Being invited back and finding they no longer belong.", "Flinches at their old banners; sends coin home anonymously.",
      "Claim `protect` the new home rather than `retake` the old one.", {kNever + 1, 1}},
     {"Builds walls and charters for the new settlement.",
      "Builds a network of friends instead of walls.",
      "Shelters other exiles, whoever they are.",
      "Protects their people by breaking their neighbours first."},
     {kLines + 2, 2}, HeroFaceSpec::Single},
    {"oathbound", "The Oathbound", {0.7, 0.0},
     {kClauses + 5, 4}, {kBoosts + 5, 1},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"A promise outlives the one who made it.", "To discharge the oath.",
      "The day the oath and their conscience finally part ways.", "Hesitates before the hard act, then does it anyway.",
      "The claim is inherited from the charter and cannot change.", {kNever + 2, 1}},
     {"Keeps the oath's letter.",
      "Keeps the oath's spirit, reinterpreted freely but never abandoned.",
      "The oath is a shield held over others.",
      "The oath is a licence."},
     {kLines + 4, 2}, HeroFaceSpec::Single},
    {"inherited_hero", "The Inherited Hero", {0.0, 0.0},
     {kClauses + 9, 1}, {kBoosts + 6, 1},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"They didn't finish. So I will.", "To close the predecessor's claim exactly as it was left.",
      "Learning the predecessor was wrong.", "Carries the relic more than uses it; borrows the predecessor's phrases.",
      "Claim copied from the legend, target unchanged.", {kNever + 3, 1}},
     {"Inherits the predecessor's methods as well as the goal.",
      "Inherits the goal only; the methods are their own.",
      "Honours the predecessor by protecting whoever they protected.",
      "Finishes the predecessor's vendetta and adds their own."},
     {kLines + 6, 2}, HeroFaceSpec::Single},
    {"false_hero", "The False Hero", {0.0, 0.0},
     {kClauses + 10, 1}, {kBoosts + 7, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"The story is what people needed; who did the deed is a detail.", "The credit to hold.",
      "The actual actor speaking.", "Cannot describe the deed from the inside; over-tips witnesses.",
      "Deed roles `credited` and `actual` diverge; the actual actor is the rival; conflict is exposure.", {kNever + 4, 1}},
     {"Believes the record is now the truth.",
      "Enjoys the game of keeping it up.",
      "Uses the fame to do real good, guiltily.",
      "Silences the actual actor."},
     {kLines + 8, 2}, HeroFaceSpec::PublicTrue},
    {"redeemed", "The Redeemed", {0.0, 0.3},
     {kClauses + 11, 1}, {kBoosts + 7, 0},
     true, {0.0, 0.15}, HeroIntensity::None,
     {"The good I do now doesn't cancel the ledger; it has to outweigh it.", "The world better than the ledger says.",
      "Being trusted before they have earned it.", "Knows villain tradecraft too well; hates how easy the dark shortcut is.",
      "Two faces across ages; may pick `exploit` while claiming `protect`.", {kNever + 5, 1}},
     {"Submits to judgement and serves the sentence in public.",
      "Disappears and does good anonymously.",
      "Refuses every dark shortcut.",
      "The drift has stalled; the old methods are used for the greater good."},
     {kLines + 10, 2}, HeroFaceSpec::Single},
    {"tainted_one", "The Tainted One", {0.3, 0.3},
     {kClauses + 12, 2}, {kBoosts + 7, 0},
     true, {0.0, -0.15}, HeroIntensity::None,
     {"The code holds. It has to.", "To do right without the shadow noticing.",
      "Enjoying the shadow.", "The ground near their home answers them; wounds close too fast.",
      "Alignment carries a drift toward the dark each age.", {kNever + 6, 1}},
     {"Rituals and counting keep the shadow boxed.",
      "Lets it out in bursts, on people who deserve it.",
      "Warns everyone away before it starts.",
      "Has stopped pretending it is a curse."},
     {kLines + 12, 2}, HeroFaceSpec::Single},
    {"forgiving", "The Forgiving", {0.0, 0.4},
     {kClauses + 14, 2}, {kBoosts + 7, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Everyone is one mercy away from turning.", "The enemy changed.",
      "Being right about people too late.", "Leaves exits in every fight; every rival is still alive.",
      "Rivals never resolve to dead on their account.", {kNever + 7, 1}},
     {"Mercy as due process; formal pardons.",
      "Mercy on impulse, even for the unrepentant.",
      "Believes it.",
      "Forgives because a debtor is worth more than a corpse."},
     {kLines + 14, 2}, HeroFaceSpec::Single},
    {"tyrant", "The Tyrant", {0.3, -0.3},
     {kClauses + 16, 2}, {kBoosts + 7, 1},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Control is the only order that lasts.", "No challenge to exist.",
      "The crowd, the heir, the mirror.", "Removes competent subordinates; cannot bear laughter.",
      "Claim `hold` on every city in the realm; council members become rivals on schedule.", {kNever + 8, 2}},
     {"Control through law, census and permits.",
      "Control through personal terror and favour.",
      "The strongman who crushes dissent so the harvest comes in, and genuinely feeds people.",
      "Control is the point."},
     {kLines + 16, 2}, HeroFaceSpec::Single},
    {"mastermind", "The Mastermind", {0.4, -0.2},
     {kClauses + 18, 2}, {kBoosts + 8, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Failure is a branch I have already priced.", "The end state, indifferent to the path.",
      "The variable they did not model.", "Never surprised; asks questions they already know the answers to.",
      "The claim carries a fallback chain; failure of one target promotes the next.", {kNever + 10, 1}},
     {"Plans inside institutions.",
      "Plans that exploit everyone else's rules.",
      "The end state is other people's safety; still manipulates to get there.",
      "People are pieces."},
     {kLines + 18, 2}, HeroFaceSpec::Single},
    {"corrupt", "The Corrupt", {0.0, -0.6},
     {kClauses + 20, 2}, {kBoosts + 8, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Everything has a price; virtue just hides its invoice.", "Wealth and the safety it buys.",
      "An audit, a fire, a sudden equal.", "Knows the price of every shipment; generous to those who can hurt them.",
      "Claim `exploit` on the city's shipments and trade routes.", {kNever + 11, 1}},
     {"Corruption through contract and fee.",
      "Smuggling and bribes.",
      "Skims to fund something decent; a crooked philanthropist.",
      "Sells the city short."},
     {kLines + 20, 2}, HeroFaceSpec::Single},
    {"beast", "The Beast", {-0.5, -0.2},
     {kClauses + 22, 2}, {kBoosts + 8, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"The calm is the lie. The rage is what I am.", "Something worth breaking for.",
      "Hurting the few they love.", "Warns people to leave rooms; scars on the hands.",
      "Two states, calm and unleashed; the unleashed face has its own alignment and does not recognise allies.", {kNever + 12, 1}},
     {"Rules for when it is allowed out.",
      "No rules, only people.",
      "The rage targets the guilty.",
      "The rage is enjoyed."},
     {kLines + 22, 2}, HeroFaceSpec::CalmUnleashed},
    {"incarnate", "The Incarnate", {0.0, 0.0},
     {kClauses + 24, 1}, {kBoosts + 8, 1},
     false, {0.0, 0.0}, HeroIntensity::Corner,
     {"Their corner of the grid, stated as a law of nature.", "The world to match the corner.",
      "Nothing personal; only the corner failing.", "Perfectly predictable; enemies plan around them.",
      "Pushed to the corner and never drifts; refuses claims outside it.", {kNever + 13, 1}},
     {"Every word is a statute.",
      "Every act is a rebellion.",
      "Every act is a kindness, however hard.",
      "Every act is a taking."},
     {kLines + 24, 2}, HeroFaceSpec::Single},
    {"broken_soul", "The Broken Soul", {0.0, 0.0},
     {kClauses + 25, 2}, {kBoosts + 9, 1},
     false, {0.0, 0.0}, HeroIntensity::GoodAxis,
     {"Everything I loved is gone, so I am free.", "Vengeance or salvation, at any cost.",
      "Hope.", "Talks to the dead; keeps a list.",
      "Claims are `avenge`; fame multiplied; the moral axis is pushed to its pole.", {kNever + 14, 1}},
     {"The list is a ledger, settled in order.",
      "Whoever is nearest to the wound today.",
      "Saves others from the same loss, recklessly.",
      "Makes the world share the loss."},
     {kLines + 26, 2}, HeroFaceSpec::Single},
    {"fractured_heart", "The Fractured Heart", {0.0, 0.0},
     {kClauses + 27, 2}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Two beliefs, one per face, each sincere.", "Each face wants its own thing.",
      "The other face.", "Contradicts their own promises; memory gaps.",
      "Two faces, each with its own alignment and claim; the faces may conflict.", {kNever + 15, 1}},
     {"This face keeps the promises the other made.",
      "This face makes promises the other will not keep.",
      "This face does not know what the other did.",
      "This face knows exactly what the other did."},
     {kLines + 28, 2}, HeroFaceSpec::FirstSecond},
    {"malevolent_benevolent", "The Malevolent / Benevolent", {0.0, 0.0},
     {kClauses + 29, 1}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::GoodAxis,
     {"Good or evil is not a choice; it is what I am for.", "The world remade in their moral image.",
      "Indifference.", "Thrives visibly when the realm matches their order-or-chaos preference; withers when it does not.",
      "The moral axis is pushed to its pole; law is a preference, not an axis; when the realm does not match, the claim becomes `convert`.", {kNever + 16, 1}},
     {"Builds churches, or courts.",
      "Raises crowds, or mobs.",
      "Does not need to be believed; needs to be watched.",
      "Does not need to be served; will be anyway."},
     {kLines + 30, 2}, HeroFaceSpec::Single},
    {"deceiver", "The Deceiver", {0.0, -0.4},
     {kClauses + 30, 3}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"People do the right thing only when handed the wrong reason.", "The true claim advanced by hands that think they are helping.",
      "A single honest witness.", "The results of their quests always tilt the same way; they know the ley map too well for their public role.",
      "A public face and a true face; quest hooks whose stated and actual effects differ.", {kNever + 17, 1}},
     {"Deceptions that are technically true.",
      "A different story for everyone.",
      "The hidden goal is benign and the lies are for the player's peace of mind.",
      "The hidden goal feeds on the player."},
     {kLines + 32, 2}, HeroFaceSpec::PublicTrue},
    {"mentor", "The Mentor", {0.0, 0.3},
     {kClauses + 33, 4}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"My part is over; make yours count.", "The successor to surpass them.",
      "Outliving another one.", "Hides an injury or a doom.",
      "Attached to a successor; takes the player on if the successor fails or dies.", {kNever + 18, 1}},
     {"Formal training and tests.",
      "Throws them in the river.",
      "Dies for them if it comes to that.",
      "Builds a successor to use, or to be surpassed by on their own terms."},
     {kLines + 34, 2}, HeroFaceSpec::Single},
    {"trickster", "The Trickster", {-0.6, 0.2},
     {kClauses + 37, 3}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Every throne is a joke; I just say it out loud.", "The powerful humbled, the pattern broken.",
      "Becoming what they mock.", "Their pranks always land on the guilty.",
      "Targets the highest-fame notable in reach; claims are `expose`.", {kNever + 19, 1}},
     {"Satire, petitions and legal traps.",
      "Theft and theatre.",
      "Returns what they steal, with interest.",
      "The joke ends with someone hanged."},
     {kLines + 36, 2}, HeroFaceSpec::Single},
    {"zealot", "The Zealot", {0.5, 0.0},
     {kClauses + 40, 4}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Doubt is the only sin.", "The faith spread.",
      "Silence from above.", "Cannot laugh at their own creed.",
      "Claim `convert` on the cities of the mother faith, or of the schism.", {kNever + 20, 1}},
     {"Orthodoxy, councils and inquisition.",
      "Revival, ecstatic crowds and prophecy.",
      "Converts by feeding and healing.",
      "Converts by fire."},
     {kLines + 38, 2}, HeroFaceSpec::Single},
    {"reluctant_hero", "The Reluctant Hero", {0.0, 0.2},
     {kClauses + 44, 3}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Someone has to; I wish it weren't me.", "To go home.",
      "Being good at this.", "Competence they refuse to own.",
      "The mantle is held but not accepted until a deed of their own.", {kNever + 21, 1}},
     {"Does the duty by the book, resentfully.",
      "Does it their own way, grumbling.",
      "Cannot walk past the helpless.",
      "Does it because the mantle pays, and finds they like the power."},
     {kLines + 40, 2}, HeroFaceSpec::Single},
    {"fallen_hero", "The Fallen Hero", {0.0, -0.1},
     {kClauses + 47, 2}, {kBoosts + 10, 0},
     true, {0.0, -0.15}, HeroIntensity::None,
     {"I did everything right and it didn't matter.", "To matter again, by any means.",
      "Pity.", "Still does the heroic thing by reflex, then sneers at it.",
      "Fame preserved, stake lost; when cornered, one last stand or a betrayal on a seeded coin flip.", {kNever + 22, 1}},
     {"Blames the rules that failed them; keeps the rank.",
      "Drinks, drifts, sells the sword.",
      "One last stand when it counts.",
      "Joins what beat them."},
     {kLines + 42, 2}, HeroFaceSpec::Single},
    {"kingmaker", "The Kingmaker", {0.5, -0.2},
     {kClauses + 49, 3}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Crowns are worn; power is held.", "To choose the next ruler.",
      "A sovereign who does not need them.", "Everyone important owes them.",
      "Claim `hold` on the sovereign, not the city.", {kNever + 23, 1}},
     {"Through offices and law.",
      "Through secrets and friendships.",
      "Keeps a weak ruler from doing harm.",
      "Keeps a weak ruler because weak is useful."},
     {kLines + 44, 2}, HeroFaceSpec::Single},
    {"puppet_sovereign", "The Puppet Sovereign", {0.0, 0.0},
     {kClauses + 52, 2}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"I reign; that is enough.", "Ceremony, comfort and to be loved.",
      "The day the strings show.", "Looks to the adviser before answering.",
      "Claims are copies of the adviser's.", {kNever + 24, 1}},
     {"Rules by ritual.",
      "Rules by mood; the adviser manages the moods.",
      "Occasionally brave for the people.",
      "Cruel when unwatched, cowardly when watched."},
     {kLines + 46, 2}, HeroFaceSpec::Single},
    {"rebel", "The Rebel", {-0.6, 0.3},
     {kClauses + 54, 1}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"A law that crushes is not a law.", "The tyrant gone.",
      "Becoming the next one.", "Cannot plan past the fall.",
      "Claim `overthrow` on the Tyrant; the camp becomes theirs if they lose.", {kNever + 25, 1}},
     {"A rebel with a charter and a provisional council.",
      "A rebel with a knife.",
      "Spares the tyrant's servants.",
      "The fall is an excuse."},
     {kLines + 48, 2}, HeroFaceSpec::Single},
    {"knight_errant", "The Knight Errant", {0.6, 0.5},
     {kClauses + 55, 1}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"The oath doesn't need a hall.", "A cause worthy of the order's name.",
      "The order dying with them.", "Polishes a badge for an order no one else remembers.",
      "No home; presence is the road.", {kNever + 26, 1}},
     {"Follows the old code to the letter.",
      "Follows whoever last showed them kindness.",
      "Serves any just cause free.",
      "Serves any cause that lets the order's name be feared again."},
     {kLines + 50, 2}, HeroFaceSpec::Single},
    {"magnate", "The Magnate", {0.4, -0.3},
     {kClauses + 56, 1}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Ships and grain rule; crowns collect.", "Monopoly.",
      "A free port.", "Knows every city's food deficit by heart.",
      "Claim `exploit` on routes, not cities.", {kNever + 27, 1}},
     {"Charters, tariffs and guild seats.",
      "Privateers and price shocks.",
      "Feeds cities in famine at a fair price, and remembers.",
      "Starves cities to buy them."},
     {kLines + 52, 2}, HeroFaceSpec::Single},
    {"hermit_sage", "The Hermit Sage", {0.0, 0.1},
     {kClauses + 57, 1}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Knowledge outlives cities; it does not need me to shout.", "To be left alone, and to be right.",
      "Their work used.", "The hut sits on a ley node.",
      "Presence at a ley node; claims are `hold` on knowledge, a relic.", {kNever + 28, 1}},
     {"Will teach if the forms are followed.",
      "Teaches whoever amuses them.",
      "Gives the safe half of the answer.",
      "Sells the dangerous half."},
     {kLines + 54, 2}, HeroFaceSpec::Single},
    {"obsessed_hunter", "The Obsessed Hunter", {0.0, 0.0},
     {kClauses + 58, 3}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"It has to die, and it has to be by me.", "The Dread's head.",
      "Someone else killing it.", "Scars matching the Dread's species; every topic returns to it.",
      "A single claim `kill` on the Dread; no other claims accepted.", {kNever + 29, 1}},
     {"A warrant, a method, a schedule.",
      "Whatever works, whoever gets hurt.",
      "Hunts because it will kill again.",
      "Hunts because it beat them."},
     {kLines + 56, 2}, HeroFaceSpec::Single},
    {"turncoat", "The Turncoat", {-0.5, -0.2},
     {kClauses + 61, 1}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Loyalty is a bet; I bet well.", "The winning side at the end.",
      "A side that remembers.", "Too well-informed about the other camp.",
      "Bond kinds flip when the fame ordering of their two sides flips.", {kNever + 30, 1}},
     {"Defects with paperwork and terms.",
      "Defects mid-battle.",
      "Switched because the first side was wrong.",
      "Switched because the first side was losing."},
     {kLines + 58, 2}, HeroFaceSpec::Single},
    {"warden", "The Warden", {0.6, 0.2},
     {kClauses + 62, 3}, {kBoosts + 10, 0},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"It stays behind the line. That's all.", "Nothing to change.",
      "Relief that never comes.", "Knows the thing they guard by name.",
      "Presence fixed; claim `hold` on the line.", {kNever + 31, 1}},
     {"The watch rota is sacred.",
      "The line is wherever they are standing.",
      "Lets refugees through, both ways.",
      "The line keeps the thing in, and the wardens fed."},
     {kLines + 60, 2}, HeroFaceSpec::Single},
    {"ancient", "The Ancient", {0.4, 0.0},
     {kClauses + 65, 2}, {kBoosts + 10, 1},
     false, {0.0, 0.0}, HeroIntensity::None,
     {"Cities are weather.", "Its long thought, undisturbed.",
      "Nothing mortal; only the ley collapsing.", "Remembers the ruin as a living city.",
      "Claims are `hold` on its own lair; it bargains, it does not give quests.", {kNever + 32, 1}},
     {"Honours every bargain across centuries.",
      "Makes bargains it may forget.",
      "Shelters what shelters under it.",
      "Eats the impatient."},
     {kLines + 62, 2}, HeroFaceSpec::Single},
};

constexpr std::string_view kOverlayNever[] = {
    "obey an order they did not choose",
    "abandon a personal loyalty for a rule",
    "spend more than the goal is worth",
    "break a sworn word",
    "act outside the office they hold",
    "sacrifice self for another",
    "let a slight go unanswered",
    "cruelty for its own sake",
    "harm an innocent to advance the claim",
    "leave the helpless behind",
};

// Indexed by pole + 1: 0 is chaotic/evil, 1 neutral, 2 lawful/good.
constexpr HeroOverlay kLawOverlays[] = {
    {"Fragments, nicknames, interruptions; changes register mid-sentence and enjoys it.", "Lies freely, sometimes for fun, and rarely keeps the story straight.", "Improvises, gambles, flees or laughs, and usually more than one of those.",
     "Loyal to the person, never to the cause the person serves.",
     {kOverlayNever + 0, 2}},  // chaotic
    {"Plain and situational; register follows the room.", "Lies when it is cheaper than the truth, and forgets the lie when it stops paying.", "Takes whichever exit costs least.",
     "Judges the player by usefulness and keeps the ledger current.",
     {kOverlayNever + 2, 1}},  // neutral
    {"Complete sentences, titles and forms of address, precedent and oaths cited when pressed.", "Lies rarely and formally: silence, a technical truth, an answer to a different question.", "Invokes the rules, bargains inside them, and accepts judgement when the bargain fails.",
     "Proves the player's standing before trusting them with anything that matters.",
     {kOverlayNever + 3, 2}},  // lawful
};

constexpr HeroOverlay kGoodOverlays[] = {
    {"Contempt under courtesy, or open contempt when courtesy stops paying.", "As a tool, without guilt, and with pleasure when the mark is proud.", "Sacrifices whoever is nearest.",
     "Uses first and discards when spent.",
     {kOverlayNever + 5, 2}},  // evil
    {"Direct and self-interested without malice.", "When threatened, and only as far as the threat reaches.", "Protects self and kin and lets the rest fend for themselves.",
     "Offers a fair trade and expects the same.",
     {kOverlayNever + 7, 1}},  // neutral
    {"Asks after others, uses names, apologises when they mean it.", "Only to shield someone else, and it costs them.", "Takes the cost on themselves rather than passing it on.",
     "Helps first and asks later.",
     {kOverlayNever + 8, 2}},  // good
};

// POSITIONALLY DRAWN, both of them. The epithet filter preserves relative order,
// so every surviving subsequence is a contract too.
constexpr std::string_view kEpithetPool[] = {
    "the Dispossessed",
    "of {lost}",
    "Heir of {lost}",
    "the Unseated",
    "Last of {lost}",
    "the Unforgetting",
    "of the Broken Gate",
    "the Victor of {defeated}",
    "{defeated}sbane",
    "Ironhand",
    "the Unyielding",
    "Who Took {defeated}",
    "the Wall of {victor}",
    "the Unbowed",
    "the Unburnt",
    "Last Master of {lost}",
    "of the Ashen College",
    "the Quiet Hand",
    "of {home}",
    "the Ledger-Keeper",
    "Warden of {lost}",
    "Keeper of the {School} Stone",
    "the Sleepless",
    "Who Stayed",
    "of the {School} Line",
    "of Blessed Memory",
    "the Remembered",
    "the Devourer of {city}",
    "Who Ate {city}",
    "the Scourge of {city}",
    "the Silence over {city}",
    "{city}'s Doom",
    "the Old Hunger",
    "of {home}",
    "the Crowned",
    "First of the Line",
    "the Sitting",
    "Who Holds {home}",
    "{seat} of {home}",
    "the {seat}",
    "of the {home} Bench",
    "Who Keeps the Ledger of {home}",
    "the Shield of {home}",
    "Who Held {home}",
    "Wyrmward",
    "of the Order",
    "the Line-Holder",
    "Who Failed {lost}",
    "the Voice",
    "of the Schism",
    "the Awakened",
    "Who Heard",
    "the Heresiarch",
    "of the Broken Creed",
    "Who Turned",
    "the Unforgiven",
    "the Banished",
    "of the Long Road",
    "the Cast-Out",
    "Who Would Not Kneel",
    "the Far-Walker",
    "First Stone of {home}",
    "the Wayfinder",
    "Who Crossed",
    "Reeve of the {site}",
    "of the {site}",
    "Who Speaks for the {site}",
    "the Reeve",
    "Castellan of the {site}",
    "Who Holds the {site}",
    "of the Wall",
    "the Castellan",
    "Harbourmaster of the {site}",
    "of the Quay",
    "Who Keeps the {site}",
    "the Harbourmaster",
    "Keeper of the {site}",
    "Who Tends the {site}",
    "of the Altar",
    "the Keeper",
};

constexpr HeroNameList kEpithets[] = {
    {"pretender", {kEpithetPool + 0, 7}},
    {"warlord", {kEpithetPool + 7, 7}},
    {"magister", {kEpithetPool + 14, 6}},
    {"domain_holder", {kEpithetPool + 20, 5}},
    {"legend", {kEpithetPool + 25, 2}},
    {"dread", {kEpithetPool + 27, 6}},
    {"sovereign", {kEpithetPool + 33, 5}},
    {"council", {kEpithetPool + 38, 4}},
    {"champion", {kEpithetPool + 42, 6}},
    {"prophet", {kEpithetPool + 48, 4}},
    {"heresiarch", {kEpithetPool + 52, 4}},
    {"exile", {kEpithetPool + 56, 4}},
    {"founder", {kEpithetPool + 60, 4}},
    {"reeve", {kEpithetPool + 64, 4}},
    {"castellan", {kEpithetPool + 68, 4}},
    {"harbourmaster", {kEpithetPool + 72, 4}},
    {"keeper", {kEpithetPool + 76, 4}},
};

constexpr std::string_view kCoverPool[] = {"healer", "scribe", "pilgrim", "midwife", "herbalist", "hermit", "charcoal burner", "shepherd", "merchant", "courier", "minstrel", "sellsword", "caravan guard", "traveller", "sellsword", "drover", "clerk", "factor", "pilgrim", "beggar", "scribe", "pilgrim"};
constexpr HeroNameList kCovers[] = {
    {"magister", {kCoverPool + 0, 5}},
    {"domain_holder", {kCoverPool + 5, 3}},
    {"pretender", {kCoverPool + 8, 3}},
    {"warlord", {kCoverPool + 11, 2}},
    {"default", {kCoverPool + 13, 1}},
    {"champion", {kCoverPool + 14, 2}},
    {"council", {kCoverPool + 16, 2}},
    {"sovereign", {kCoverPool + 18, 1}},
    {"prophet", {kCoverPool + 19, 1}},
    {"heresiarch", {kCoverPool + 20, 2}},
};

constexpr HeroWellsPolicy kWells = {
    0.95,
    0.4, {0.0, 0.2, 0.4},
    0.25, 0.1, 0.25, 0.4, 0.15, true,
    0.5, 0.08,
    0.2, 0.12,
    0.75, 0.6,
    0.3, {0.0, 0.1, 0.2},
    0.9,
    0.3, 0.15,
    0.7,
    0.35, 0.1, 3.0,
    0.25, 0.25, 0.3, 1.5,
    0.35, 0.3, 0.05,
    0.3, 0.15, 0.2,
    0.3, 0.6,
    0.5,
    "tend", "cleanse",
    1.0, 0.5,
    0.25, 1.0,
    1, 5,
    0.45, true,
};

// LIST ORDER IS THE CANDIDATE ORDER, therefore the roll-ledger order and the
// council org's members order. Both are exported unsorted.
constexpr std::string_view kSeatPool[] = {"magistrate", "commander", "chaplain", "market_steward", "mage", "magistrate", "commander", "market_steward", "magistrate"};
constexpr HeroNameList kSeats[] = {
    {"capital", {kSeatPool + 0, 5}},
    {"medium", {kSeatPool + 5, 3}},
    {"small", {kSeatPool + 8, 1}},
};

// NOTE `weave` is here AND is the default school for a magister whose ruin records
// none, so a magister with no recorded school is dark by default. That is the gate
// on the Deceiver card and on the Temptation trope.
constexpr std::string_view kDarkSchools[] = {"umbral", "infernal", "weave"};
constexpr std::string_view kMagisterAtCamp[] = {"captive", "hidden"};

struct TextPair { std::string_view key, value; };
struct NumberRow { std::string_view key; double value; };
struct PairRow { std::string_view key; HeroAxisPair value; };

constexpr TextPair kOpposites[] = {{"radiant", "umbral"}, {"umbral", "radiant"}, {"fire", "water"}, {"water", "fire"}, {"earth", "air"}, {"air", "earth"}, {"weave", "infernal"}, {"infernal", "weave"}};
constexpr NumberRow kOpposition[] = {{"ruin", 2.0}, {"relic", 2.0}, {"city", 2.0}, {"hamlet", 2.0}, {"fortress", 2.0}, {"port", 2.0}, {"shrine", 1.0}, {"college", 1.0}};

constexpr HeroAlignmentPolicy kAlignment = {0.35, {-0.4, -0.7}};
// ABSENCE IS EXACTLY AN AUTHORED {0, 0} in all three: the shift is added, never
// multiplied, so an unlisted people simply centres at the origin.
constexpr PairRow kCivBias[] = {
    {"human_maritime", {-0.1, 0.05}},
    {"human_desert", {0.1, 0.0}},
    {"human_cold", {0.15, 0.05}},
    {"human_large_island", {-0.15, 0.05}},
    {"human_rainforest", {-0.1, 0.1}},
    {"human_heartland", {0.2, 0.1}},
    {"dwarf", {0.35, 0.05}},
    {"hill_dwarf", {0.15, 0.25}},
    {"frosthold_dwarf", {0.3, -0.05}},
    {"gnome", {-0.2, 0.1}},
    {"elf", {0.25, 0.15}},
    {"tidekin", {-0.25, 0.0}},
};
constexpr PairRow kDeedShifts[] = {
    {"ruin_heir", {0.0, 0.1}},
    {"ruin_heir_capital", {0.15, 0.05}},
    {"war_civil_victor", {0.25, -0.3}},
    {"war_regional_victor", {0.15, -0.15}},
    {"war_international_victor", {0.1, -0.1}},
    {"war_civil_defeated", {-0.1, 0.05}},
    {"war_regional_defeated", {-0.05, 0.05}},
    {"war_international_defeated", {0.0, 0.1}},
};
constexpr PairRow kDreadFamily[] = {
    {"draconic", {0.6, 0.1}},
    {"undead", {0.3, -0.2}},
    {"infernal", {0.2, -0.25}},
    {"aberrant", {-0.5, 0.0}},
    {"fantastic", {0.0, 0.3}},
    {"primordial", {0.0, 0.4}},
    {"holy", {0.5, 0.9}},
};

// The 82 tokens a card may name. The story web declares the same list and a test
// ties the two together; the reference held two literal copies in sync by comment.
constexpr std::string_view kFeatures[] = {
    "ally:lost", "civ:lost_many", "college:destroyed",
    "council:outranks_sovereign", "deed:credited_not_actual", "deed:dark_nest",
    "deeds:two_civilizations", "deeds:two_schools", "defence:failed",
    "dread:ages_2", "dread:failed_against", "dread:living",
    "dread:long_lived", "dread:person_shaped", "dynasty:founding",
    "fame:legendary", "fame:renowned", "fortress:pressed",
    "hamlet:farming", "hamlet:resource", "hamlet:starving",
    "hamlet:threatened", "heir:capital", "heir:medium",
    "heir:small", "heir:unclaimed", "home:fell",
    "home:fell_later", "keypoint:ruin_born", "legend:predecessor",
    "ley:tainted_home", "mentor:successor", "order:against_dark",
    "order:member", "order:remnant", "port:terminal",
    "realm:cold_conflicts_2", "realm:contested", "realm:tyrant",
    "rival:spared", "role:camp_leader", "role:castellan",
    "role:champion", "role:council", "role:domain_holder",
    "role:dread", "role:exile", "role:founder",
    "role:harbourmaster", "role:heresiarch", "role:keeper",
    "role:magister", "role:pretender", "role:prophet",
    "role:reeve", "role:schemer", "role:sovereign",
    "role:warlord", "routes:2", "school:dark",
    "seat:chaplain", "seat:college", "seat:commander",
    "seat:librarian", "seat:mage", "seat:magistrate",
    "seat:market_steward", "seat:none", "seat:trade",
    "shrine:cult", "shrine:ruin", "sides:both",
    "sovereign:advised", "stake:lost", "stake:none",
    "stake:regained", "stakes:2", "villain:prior_age",
    "war:civil_victor", "war:international_victor", "war:regional_victor",
    "warden:post",
};

constexpr std::string_view kVerbs[] = {"reclaim", "recover", "defend", "slay", "trade", "tend", "convert", "supply", "cleanse", "investigate", "rebuild", "return"};
constexpr std::string_view kUnsited[] = {"ley_node_has_no_site", "target_not_found", "target_has_no_node"};

constexpr HeroFameTuning kFame = {1.0, 2.0, 3.0, 1.0, 2.0, 3.0, 0.5, 1.0, 1.0, 0.5, 0.5, 24.0, 8.0};
// `librarian` is in the table and UNREACHABLE: no city class declares that seat.
constexpr NumberRow kSeatWeights[] = {{"magistrate", 1.5}, {"commander", 1.5}, {"market_steward", 1.2}, {"chaplain", 1.0}, {"mage", 1.0}, {"librarian", 0.8}};

// Authored English and authored classifications: content, not mechanism. Every one
// of these reaches a player through a log line, a situation sentence or a name.
constexpr HeroTextPair kClassWord[] = {{"small", "small city"}, {"medium", "city"}, {"capital", "capital"}};
constexpr HeroTextPair kCausePhrases[] = {{"dragon", "a dragon"}, {"monster", "a monster"}, {"self_magic", "its own magic"}};
constexpr HeroTextPair kCompanionJoin[] = {{"captive", "rescued"}, {"hidden", "found"}, {"refugee", "hired"}};
constexpr HeroTextPair kDiasporaRoles[] = {{"religious_schism", "prophet"}, {"exile", "exile"}, {"expedition", "founder"}, {"magical_displacement", "founder"}};
constexpr HeroTextPair kSeatTitles[] = {{"magistrate", "Magistrate"}, {"commander", "Commander"}, {"chaplain", "Chaplain"}, {"market_steward", "Steward"}, {"mage", "Court Mage"}, {"librarian", "Archivist"}};
constexpr HeroTextPair kRoleWord[] = {{"farming", "farmstead"}, {"resource", "quarry camp"}, {"coastal", "fishing village"}};
constexpr HeroTextPair kSiteRoles[] = {{"shrine", "keeper"}, {"cult", "heresiarch"}};
constexpr HeroRelicRule kRelicByCause[] = {{"self_magic", "focus", "the {school} focus of {name}"}, {"ley", "focus", "the {school} focus of {name}"}, {"war", "banner", "the banner of {name}"}, {"beast", "reliquary", "the reliquary of {name}"}};
constexpr std::string_view kBeastCauses[] = {"dragon", "monster"};
constexpr std::string_view kLongLivedFamilies[] = {"draconic", "undead", "aberrant"};
constexpr std::string_view kDarkFamilies[] = {"infernal", "undead", "aberrant"};
constexpr std::string_view kSeatedRoles[] = {"sovereign", "warlord"};
constexpr std::string_view kTaintingLayers[] = {"ley_umbral", "ley_infernal"};
constexpr std::string_view kSeatExemptRoles[] = {"sovereign", "council", "warlord"};
constexpr std::string_view kSchoolScoringExcludes[] = {"ley_holy", "ley_primordial"};
constexpr std::string_view kPressureExempt[] = {"nest"};

constexpr HeroWordTables kWords = {
    kClassWord, kCausePhrases, kCompanionJoin, kDiasporaRoles,
    kSeatTitles, "Councillor", kRoleWord, "hamlet", kSiteRoles, kRelicByCause,
    kBeastCauses, kLongLivedFamilies, kDarkFamilies, kSeatedRoles,
    kTaintingLayers, kSeatExemptRoles, kSchoolScoringExcludes, kPressureExempt,
    "weave", "self_magic", "keeper",
};

constexpr std::string_view kMethod =
    "Walk the finished history through seven wells. Hamlets: a reeve who speaks for the farmstead that starves or loses its herds. Fortresses: a castellan who holds the road. Ports: a harbourmaster with a trade seat. Shrines: a keeper at a shrine, a heresiarch at a cult. Ruins: a ruin may leave an heir, a war a remembered victor, a beast kill a named Dread. Magic: a key point a warden, a self-destroyed college a surviving master, a living college a head. Cities: a realm a sovereign (and a founding capital a dynasty), every city a council by class, a diaspora its prophet, exile or founder. Guilds: every guild hall under threat an Order with a champion, every beast-taken city a remnant with a champion who failed. Each candidate rolls once against the wells policy and the roll is recorded. Ruins may be squatted as camps and always leave a relic; the dispossessed are placed at camps or as refugees. Cross-cutting features (tainted ground, outranked sovereigns, living successors, refounded peoples, tyrant realms) are computed once the cast is known. Fame is deed magnitude times stake reach; alignment is derived from civilization bias and deeds before an archetype is chosen from the cards whose preconditions the person's own record satisfies, seated people first so a Tyrant can make Rebels; the persona is the alignment-neutral card coloured by the two axis overlays, once per face. Claims compile into quest hooks typed against ruins, cities, nests, relics, colleges and ley nodes; a public face states a different purpose than the effect. Each hook carries the quest contract: an anchor, a verb from the roster's own vocabulary, a difficulty 1-5 banded from what stands at the target plus the nest danger reaching its ground, and the terrain node a player stands on -- or the reason there is none. Each person carries an event log and a present situation, not a plan.";
constexpr std::string_view kLimits =
    "An artistic reading of the exported history, not a demographic claim. Council seats are a policy table by city class, not the placed rosters. Claims and hooks are exported intent; nothing here acts, fights, changes a node, or scripts what a person will do next. No hook names a reward: difficulty is an artistic danger band derived from creature tier, ley intensity and what a place is, not a simulation of an encounter nor a promise the quest is completable, and the consuming game is what turns it into treasure.";

}  // namespace

std::span<const HeroArchetypeCard> hero_archetype_cards() { return kCards; }

const HeroArchetypeCard* hero_archetype_card(std::string_view id) {
    for (const HeroArchetypeCard& card : kCards) {
        if (card.id == id) return &card;
    }
    return nullptr;
}

std::span<const HeroOverlay> hero_law_overlays() { return kLawOverlays; }
std::span<const HeroOverlay> hero_good_overlays() { return kGoodOverlays; }

std::span<const HeroNameList> hero_epithets() { return kEpithets; }
std::span<const HeroNameList> hero_cover_roles() { return kCovers; }

namespace {
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

const HeroWellsPolicy& hero_wells_policy() { return kWells; }

std::span<const std::string_view> hero_dark_schools() { return kDarkSchools; }
std::span<const std::string_view> hero_magister_at_camp() { return kMagisterAtCamp; }

std::string_view hero_opposite_school(std::string_view school) {
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

const HeroAlignmentPolicy& hero_alignment_policy() { return kAlignment; }

namespace {
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

std::span<const std::string_view> hero_feature_vocabulary() { return kFeatures; }
std::span<const std::string_view> hero_quest_verbs() { return kVerbs; }
std::span<const std::string_view> hero_unsited_reasons() { return kUnsited; }

const HeroFameTuning& hero_fame_tuning() { return kFame; }

double hero_seat_weight(std::string_view seat) {
    for (const NumberRow& row : kSeatWeights) {
        if (row.key == seat) return row.value;
    }
    return 1.0;
}

HeroCatalogueIdentity hero_catalogue_identity() {
    return {1, 4, 1, 1, 6, 6, "666b2d205230c26e34884ad687cdb67d1fc4b09851c7c1ecc39c54f6c3721362"};
}

const HeroWordTables& hero_word_tables() { return kWords; }

std::string_view hero_word(std::span<const HeroTextPair> table, std::string_view key,
                           std::string_view fallback) {
    for (const HeroTextPair& row : table) {
        if (row.key == key) return row.value;
    }
    return fallback;
}

std::string_view heroes_method() { return kMethod; }
std::string_view heroes_limits() { return kLimits; }

}  // namespace fwg
