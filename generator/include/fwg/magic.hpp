// Leyline networks: independently seeded sacred alignments, and the fields they cast.
//
// Each school is its own network of key points and the lines between them. A cell's
// potency in a school is how close it is to that network, saturated so overlapping
// sources never run away. Where one school leads the rest by a clear margin, the
// ground belongs to it — which is what later stages read to mutate biomes, site
// colleges and decide what a region is.
//
// Eight KNOWN schools are the world's taxonomy. Four HIDDEN ones exist in the table but
// generation can never raise them: their occurrence is locked to zero in the option
// registry, and only the corruption API places a node in one. Their position matters
// anyway — `dominant_magic` exports an index into this table.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fwg/config.hpp"
#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

struct MagicSchool {
    std::string_view name;
    std::string_view group;        // Raw magic | Holy / unholy | Primordial | Outside
    std::string_view descriptors;  // what the school is about, in words
    int color[3];
    bool hidden;  // generation never raises one of these
};

// In contract order: the known eight first, hidden schools appended.
std::span<const MagicSchool> magic_school_table();

// Index into that table, or -1.
int magic_school_index(std::string_view name);

struct LeyNode {
    std::string id;
    Vec3 direction;
    double intensity = 0.0;
};

struct LeyEdge {
    std::string id;
    int from = 0;
    int to = 0;
    double intensity = 0.0;
};

// How the key points were scattered. Recorded because it is the only description of a
// network's shape that survives into the world document.
struct NetworkDistribution {
    int version = 2;
    std::vector<int> aligned_nodes;  // the three that share a straight spherical alignment
    int clusters = 0;
    double spread = 0.0;
    double outlier_fraction = 0.0;
    double extra_link_probability = 0.0;
};

struct LeyNetwork {
    std::string name;
    std::string group;
    std::string descriptors;
    int color[3] = {0, 0, 0};
    std::uint32_t seed = 0;
    double strength = 0.0;
    double width_m = 0.0;
    double instability = 0.0;
    std::vector<LeyNode> nodes;
    std::vector<LeyEdge> edges;
    bool manifested = false;
    NetworkDistribution distribution;

    // Set by the astrology pass, and only for the known eight: a hidden school takes
    // no surge because alien magic does not answer to this world's sky.
    bool has_surge = false;
    double surged_strength = 0.0;
};

struct MagicReport {
    int version = 4;
    bool enabled = false;
    std::map<std::string, LeyNetwork> networks;
    double mutation_threshold = 0.35;
    double dominance_margin = 0.08;
};

// A great-circle arc, resolved once so distance queries against it are cheap.
struct ArcFrame {
    Vec3 a;
    Vec3 b;
    Vec3 tangent;
    double angle = 0.0;
};
ArcFrame arc_frame(const Vec3& a, const Vec3& b);

// A ley line's `path` as the world document publishes it: the great-circle arc from `a` to
// `b` sampled at kLeyPathSegments equal angles, a first and b last. A pure function of the
// two key points, so it is derived where it is written rather than stored on the line.
inline constexpr int kLeyPathSegments = 32;
std::vector<Vec3> arc_path(const Vec3& a, const Vec3& b);

// Angular distance from p to the arc — to its interior if p projects onto it, to the
// nearer endpoint otherwise.
double distance_to_frame(const Vec3& p, const ArcFrame& frame);

// Uneven seeded clusters, scattered outliers and independently variable links. Three
// key points share a straight spherical alignment when there are at least three: a
// fantasy reading of ley alignments, not a physical law.
struct NetworkGeometry {
    std::vector<Vec3> nodes;
    std::vector<std::pair<int, int>> edges;
    NetworkDistribution distribution;
};
NetworkGeometry network_geometry(std::uint32_t seed, int count);

// The strongest school at a cell, or -1 where none leads clearly enough. `potencies`
// is indexed by the school table.
int dominant_school(const std::vector<double>& potencies, double threshold, double margin);

// Physical radius of the reference world: design radius 10000 at the recipe scale.
// School widths are authored against it and scale with the world, or a 110 m Gaussian
// falls between raster cells on a 200 km planet and every magic field collapses to zero.
inline constexpr double kReferenceRadiusM = 1774.4123532462844;

// The hard ceiling a network edit enforces on any intensity.
inline constexpr double kLeyIntensityMax = 4.0;

// Each hidden school draws on something the known eight do not. These only ever act on a
// network that already has a node, so a generated world -- whose hidden networks are always
// empty -- takes none of these paths and its fields are untouched. The corruption API is
// what puts a node in one.
inline constexpr double kBloodFeedGain = 2.5;     // dense living multiplies what blood can draw
inline constexpr double kEldritchFeedGain = 2.5;  // aberrant and undead nests and ruins feed eldritch
inline constexpr double kRotReachWidths = 6.0;    // how far rot creeps, in its own network widths
inline constexpr double kRotDecayWidths = 2.0;    // e-folding length of that creep, likewise
inline constexpr int kRotMaxSteps = 64;
inline constexpr double kVoidBite = 0.8;          // share of void potency taken from every other school

// At most this many player leyline edits in one request. Each edit the reference makes
// copies a whole network, which is why the bound is small.
inline constexpr std::size_t kMaxLeylineEdits = 128;

// A network edit's whole contract, as an append: an id non-empty and unique in the
// network, a finite unit direction, and an intensity in [0, kLeyIntensityMax]. The
// reference's edit_network copies, validates and appends; validating then appending is
// behaviourally identical and linear across a cluster. The validation is the point: a node
// the generator would refuse is one that silently breaks a field measurement later.
//
// Every edit that adds a key point goes through this one check -- a ruin's at an age turn,
// a visitation's avatar cluster, a corruption's. Throws std::invalid_argument naming the
// node; the APIs rethrow it as their own refusal.
void plant_key_point(LeyNetwork& net, const LeyNode& node);

// A refused player ley edit, naming the field of the edit that refused.
class LeylineEditError : public std::invalid_argument {
public:
    LeylineEditError(std::string field, const std::string& message)
        : std::invalid_argument(message), field_(std::move(field)) {}
    const std::string& field() const { return field_; }

private:
    std::string field_;
};

// One player edit to a KNOWN school's network. Exactly one of `node_id`, `line_id` and
// `new_node`. `intensity` sets an existing node or line and must not accompany `new_node`,
// which carries its own. A hidden school is refused: players edit what they can see.
struct LeylineEdit {
    std::string school;
    std::optional<std::string> node_id;
    std::optional<std::string> line_id;
    std::optional<double> intensity;
    std::optional<LeyNode> new_node;
};

// Validate and apply one edit in place: the reference's edit_network contract plus the
// request boundary's -- a known school, and a new node's intensity inside new_node rather
// than beside it. The caller re-evaluates the fields; geometry is never rerolled.
void apply_leyline_edit(MagicReport& magic, const LeylineEdit& edit);

struct World;

// Lays down the networks, then evaluates their fields.
void generate_networks(World& world);

// Rebuilds only the magic-derived layers, leaving the network inputs alone — so an
// editor can move a key point and see the fields follow without rerolling geometry.
//
// The hidden schools read the world as it stands: blood feeds on the living (every city,
// weighted by its share of the largest), eldritch on the aberrant and undead nests and on
// the ruins, rot creeps outward cell to cell in metres, and void subtracts from every other
// school. Each reads the world BEFORE the rebuild that follows it, so a corruption is always
// one rebuild behind the cities it is eating.
void evaluate_networks(World& world);

}  // namespace fwg
