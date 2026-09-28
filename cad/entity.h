#pragma once

#include "geom.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cad {

enum class EType : uint8_t { Line, Circle, Arc, Polyline, Ellipse, Text, MText, Hatch, Dim };
enum class DimType : uint8_t { Linear, Aligned, Radius, Diameter };
enum class LinDir : uint8_t { Horizontal, Vertical };
enum class HatchPat : uint8_t { Solid, ANSI31, ANSI37 };

// Identifies a characteristic point of an entity (used by grips, osnap and associative dimensions).
enum class KeyKind : uint8_t { None, Vertex, Mid, Center, Quad, DimLine };
struct PKey {
    KeyKind kind = KeyKind::None;
    int idx = 0;
    bool operator==(const PKey&) const = default;
};

struct PointRef {
    uint32_t ent = 0;
    PKey key;
    bool valid() const { return ent != 0 && key.kind != KeyKind::None; }
};

struct Vtx {
    Vec2 p;
    double bulge = 0;
};

// One "fat" value type for all entities keeps undo snapshots and copying trivial.
struct Entity {
    uint32_t id = 0;
    EType type = EType::Line;
    int color = 7;      // Color index (1-9); 7 = white
    uint32_t group = 0; // 0 = not grouped

    // Line (2 verts), Polyline (n verts, bulges), Text/MText (v[0] = insertion)
    std::vector<Vtx> v;
    bool closed = false;

    // Circle / Arc (CCW from a0 to a1) / Ellipse
    Vec2 c;
    double r = 0, a0 = 0, a1 = 0;
    Vec2 major;          // ellipse: center -> end of major axis
    double ratio = 1.0;  // ellipse: minor / major

    // Text / MText
    std::string text;
    double height = 2.5, rot = 0, width = 0; // width: mtext wrap width (0 = no wrap)

    // Hatch
    HatchPat pat = HatchPat::ANSI31;
    double pscale = 1.0, pangle = 0.0;
    std::vector<uint32_t> bnd;              // associative boundary entities (closed curves)
    bool has_seed = false;
    Vec2 seed;                              // internal pick point (for region-detected boundaries)
    std::vector<std::vector<Vec2>> loops;   // resolved boundary loops (world space)

    // Dimension
    DimType dt = DimType::Linear;
    LinDir ldir = LinDir::Horizontal;
    PointRef r1, r2;    // associative definition points (linear/aligned)
    uint32_t rent = 0;  // referenced circle/arc (radius/diameter)
    Vec2 p1, p2;        // last resolved definition points
    double off = 0;     // linear/aligned: dimension line offset from p1 along the normal
    Vec2 loc;           // radial: text location relative to center
    std::string override_text;
};

struct DimStyle {
    double scale = 1.0;   // DIMSCALE
    double txt = 2.5;     // DIMTXT
    double asz = 2.5;     // DIMASZ
    double exo = 0.625;   // DIMEXO
    double exe = 1.25;    // DIMEXE
    double gap = 0.625;   // DIMGAP
    int prec = 2;         // DIMDEC
};

struct Doc {
    std::vector<Entity> ents;
    uint32_t next_id = 1;
    uint32_t next_group = 1;
    uint64_t rev = 0; // bumped on every modification
    DimStyle dimstyle;

    Entity* find(uint32_t id);
    const Entity* find(uint32_t id) const;
    uint32_t add(Entity e);
    void erase(uint32_t id);
    void touch() { rev++; }
};

// --- Construction helpers ---
Entity make_line(Vec2 a, Vec2 b);
Entity make_circle(Vec2 c, double r);
Entity make_arc(Vec2 c, double r, double a0, double a1);
Entity make_polyline(const std::vector<Vtx>& v, bool closed);
Entity make_ellipse(Vec2 c, Vec2 major, double ratio);
Entity make_text(Vec2 p, double h, double rot, const std::string& s);
Entity make_mtext(Vec2 p, double w, double h, const std::string& s);
Entity make_arc_from_seg(const Seg& s);

// --- Queries ---
bool is_curve(const Entity& e);
bool is_closed_curve(const Entity& e);
std::vector<Seg> entity_segs(const Entity& e);
std::vector<Vec2> ellipse_points(const Entity& e, int n);
std::vector<Vec2> closed_loop(const Entity& e, int arc_steps = 128);

bool key_point(const Doc& d, const Entity& e, PKey k, Vec2& out);
std::vector<std::pair<PKey, Vec2>> grips(const Doc& d, const Entity& e);
void move_key(const Doc& d, Entity& e, PKey k, Vec2 np); // caller must Doc::touch()
void transform(Entity& e, const Xform& x);

BBox bbox(const Doc& d, const Entity& e);
double hit_dist(const Doc& d, const Entity& e, Vec2 p, double tol);
bool inside_window(const Doc& d, const Entity& e, const BBox& w);
bool crosses_window(const Doc& d, const Entity& e, const BBox& w);

// --- Text metrics (implemented with ImGui font data) ---
constexpr double TEXT_CAP_RATIO = 0.72; // cap height / font size
Vec2 text_extent(const Entity& e);            // local width/height in world units
std::vector<Vec2> text_box(const Entity& e);  // 4 world-space corners

// --- Dimensions ---
struct DimGeom {
    std::vector<std::pair<Vec2, Vec2>> lines;
    struct Arrow { Vec2 tip, dir; };
    std::vector<Arrow> arrows;
    Vec2 text_pos;      // bottom-center of the text
    double text_rot = 0;
    double text_h = 2.5;
    std::string text;
    double value = 0;
    Vec2 grip;          // dimension line / text grip
};
bool dim_points(const Doc& d, const Entity& e, Vec2& p1, Vec2& p2);
bool dim_circle(const Doc& d, const Entity& e, Vec2& c, double& r);
Vec2 dim_direction(const Entity& e, Vec2 p1, Vec2 p2);
DimGeom dim_geom(const Doc& d, const Entity& e);
double dim_value(const Doc& d, const Entity& e);
std::string format_number(double v, int prec);
// Change the measured value by editing the referenced geometry.
// scale=false stretches the side of the geometry at the second point; scale=true scales it about the first.
bool drive_dim(Doc& d, uint32_t dim_id, double new_value, bool scale);

// --- Associativity ---
void update_associative(Doc& d);

// --- Hatch boundary detection ---
bool find_boundary(const Doc& d, Vec2 seed, std::vector<std::vector<Vec2>>& loops);
bool loops_from_entities(const Doc& d, const std::vector<uint32_t>& ids, std::vector<std::vector<Vec2>>& loops);

// --- Modify operations ---
// Quick-mode TRIM: removes the piece of `id` around `pick` bounded by the nearest intersections.
enum class TrimResult { Trimmed, Deleted, Nothing };
TrimResult trim_entity(Doc& d, uint32_t id, Vec2 pick);
bool extend_entity(Doc& d, uint32_t id, Vec2 pick);
int join_entities(Doc& d, const std::vector<uint32_t>& ids, std::vector<uint32_t>& created);
int explode_entities(Doc& d, const std::vector<uint32_t>& ids);
std::vector<Entity> entities_from_segs(const std::vector<Seg>& segs, bool closed, const Entity& proto);

const char* type_name(const Entity& e);

} // namespace cad
