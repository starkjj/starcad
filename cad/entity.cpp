#include "entity.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>

namespace cad {

ImFont* g_text_font = nullptr; // set by the renderer; used for text metrics

// ---------------------------------------------------------------------------
// Doc
// ---------------------------------------------------------------------------
Entity* Doc::find(uint32_t id) {
    for (auto& e : ents) if (e.id == id) return &e;
    return nullptr;
}
const Entity* Doc::find(uint32_t id) const {
    for (auto& e : ents) if (e.id == id) return &e;
    return nullptr;
}
uint32_t Doc::add(Entity e) {
    e.id = next_id++;
    ents.push_back(std::move(e));
    touch();
    return ents.back().id;
}
void Doc::erase(uint32_t id) {
    std::erase_if(ents, [&](const Entity& e) { return e.id == id; });
    touch();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
Entity make_line(Vec2 a, Vec2 b) {
    Entity e; e.type = EType::Line; e.v = {{a, 0}, {b, 0}}; return e;
}
Entity make_circle(Vec2 c, double r) {
    Entity e; e.type = EType::Circle; e.c = c; e.r = r; return e;
}
Entity make_arc(Vec2 c, double r, double a0, double a1) {
    Entity e; e.type = EType::Arc; e.c = c; e.r = r; e.a0 = norm_angle(a0); e.a1 = norm_angle(a1); return e;
}
Entity make_polyline(const std::vector<Vtx>& v, bool closed) {
    Entity e; e.type = EType::Polyline; e.v = v; e.closed = closed; return e;
}
Entity make_ellipse(Vec2 c, Vec2 major, double ratio) {
    Entity e; e.type = EType::Ellipse; e.c = c; e.major = major; e.ratio = ratio;
    if (e.ratio > 1.0) { e.major = perp(major) * ratio; e.ratio = 1.0 / ratio; }
    return e;
}
Entity make_text(Vec2 p, double h, double rot, const std::string& s) {
    Entity e; e.type = EType::Text; e.v = {{p, 0}}; e.height = h; e.rot = rot; e.text = s; return e;
}
Entity make_mtext(Vec2 p, double w, double h, const std::string& s) {
    Entity e; e.type = EType::MText; e.v = {{p, 0}}; e.width = w; e.height = h; e.text = s; return e;
}
Entity make_arc_from_seg(const Seg& s) {
    if (s.sw >= 0) return make_arc(s.c, s.r, s.a0, s.a0 + s.sw);
    return make_arc(s.c, s.r, s.a0 + s.sw, s.a0);
}

const char* type_name(const Entity& e) {
    switch (e.type) {
    case EType::Line: return "Line";
    case EType::Circle: return "Circle";
    case EType::Arc: return "Arc";
    case EType::Polyline: return "Polyline";
    case EType::Ellipse: return "Ellipse";
    case EType::Text: return "Text";
    case EType::MText: return "MText";
    case EType::Hatch: return "Hatch";
    case EType::Dim:
        switch (e.dt) {
        case DimType::Linear: return "Rotated Dimension";
        case DimType::Aligned: return "Aligned Dimension";
        case DimType::Radius: return "Radial Dimension";
        case DimType::Diameter: return "Diametric Dimension";
        }
    }
    return "Entity";
}

// ---------------------------------------------------------------------------
// Curves
// ---------------------------------------------------------------------------
static double arc_sweep(const Entity& e) {
    double sw = norm_angle(e.a1 - e.a0);
    return sw < 1e-12 ? TAU : sw;
}

bool is_curve(const Entity& e) {
    switch (e.type) {
    case EType::Line: case EType::Circle: case EType::Arc: case EType::Polyline: case EType::Ellipse: return true;
    default: return false;
    }
}

bool is_closed_curve(const Entity& e) {
    return e.type == EType::Circle || e.type == EType::Ellipse || (e.type == EType::Polyline && e.closed && e.v.size() >= 2);
}

std::vector<Vec2> ellipse_points(const Entity& e, int n) {
    std::vector<Vec2> pts;
    pts.reserve(n);
    Vec2 mi = perp(e.major) * e.ratio;
    for (int i = 0; i < n; i++) {
        double t = TAU * i / n;
        pts.push_back(e.c + e.major * std::cos(t) + mi * std::sin(t));
    }
    return pts;
}

std::vector<Seg> entity_segs(const Entity& e) {
    std::vector<Seg> s;
    switch (e.type) {
    case EType::Line:
        s.push_back(Seg::line(e.v[0].p, e.v[1].p));
        break;
    case EType::Circle:
        s.push_back(Seg::arc_of(e.c, e.r, 0, TAU));
        break;
    case EType::Arc:
        s.push_back(Seg::arc_of(e.c, e.r, e.a0, arc_sweep(e)));
        break;
    case EType::Polyline: {
        size_t n = e.v.size();
        size_t cnt = e.closed ? n : n - 1;
        for (size_t i = 0; i < cnt && n >= 2; i++) s.push_back(bulge_seg(e.v[i].p, e.v[(i + 1) % n].p, e.v[i].bulge));
        break;
    }
    case EType::Ellipse: {
        auto p = ellipse_points(e, 96);
        for (size_t i = 0; i < p.size(); i++) s.push_back(Seg::line(p[i], p[(i + 1) % p.size()]));
        break;
    }
    default: break;
    }
    return s;
}

std::vector<Vec2> closed_loop(const Entity& e, int arc_steps) {
    std::vector<Vec2> pts;
    if (e.type == EType::Ellipse) return ellipse_points(e, arc_steps);
    for (auto& s : entity_segs(e)) seg_tessellate(s, pts, arc_steps, false);
    return pts;
}

// ---------------------------------------------------------------------------
// Text metrics
// ---------------------------------------------------------------------------
static Vec2 measure(const std::string& s, double height, double wrap_world) {
    double px = height / TEXT_CAP_RATIO; // font size in world units
    if (!g_text_font || !ImGui::GetCurrentContext()) {
        size_t lines = 1 + std::count(s.begin(), s.end(), '\n');
        return {0.6 * px * s.size(), px * 1.2 * lines};
    }
    const float ref = 64.0f;
    double k = px / ref;
    float wrap = wrap_world > 0 ? (float)(wrap_world / k) : 0.0f;
    ImVec2 sz = g_text_font->CalcTextSizeA(ref, FLT_MAX, wrap, s.c_str(), s.c_str() + s.size());
    return {sz.x * k, sz.y * k};
}

Vec2 text_extent(const Entity& e) {
    if (e.type == EType::Text) return {measure(e.text, e.height, 0).x, e.height};
    Vec2 m = measure(e.text.empty() ? std::string(" ") : e.text, e.height, e.width);
    return {e.width > 0 ? e.width : m.x, m.y};
}

std::vector<Vec2> text_box(const Entity& e) {
    Vec2 ext = text_extent(e);
    Vec2 u = dir(e.rot), w = perp(u), p = e.v[0].p;
    if (e.type == EType::Text) {
        Vec2 d0 = w * (-0.25 * e.height); // include descenders a bit
        return {p + d0, p + u * ext.x + d0, p + u * ext.x + w * ext.y, p + w * ext.y};
    }
    return {p, p + u * ext.x, p + u * ext.x - w * ext.y, p - w * ext.y};
}

static double text_width(const std::string& s, double h) { return measure(s, h, 0).x; }

// ---------------------------------------------------------------------------
// Dimensions
// ---------------------------------------------------------------------------
std::string format_number(double v, int prec) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", prec, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

bool dim_points(const Doc& d, const Entity& e, Vec2& p1, Vec2& p2) {
    p1 = e.p1; p2 = e.p2;
    if (e.r1.valid()) if (auto* r = d.find(e.r1.ent)) key_point(d, *r, e.r1.key, p1);
    if (e.r2.valid()) if (auto* r = d.find(e.r2.ent)) key_point(d, *r, e.r2.key, p2);
    return true;
}

bool dim_circle(const Doc& d, const Entity& e, Vec2& c, double& r) {
    c = e.c; r = e.r;
    if (e.rent) if (auto* t = d.find(e.rent)) if (t->type == EType::Circle || t->type == EType::Arc) { c = t->c; r = t->r; }
    return r > 0;
}

Vec2 dim_direction(const Entity& e, Vec2 p1, Vec2 p2) {
    if (e.dt == DimType::Aligned) return dist(p1, p2) > EPS ? norm(p2 - p1) : Vec2{1, 0};
    return e.ldir == LinDir::Horizontal ? Vec2{1, 0} : Vec2{0, 1};
}

double dim_value(const Doc& d, const Entity& e) {
    if (e.dt == DimType::Radius || e.dt == DimType::Diameter) {
        Vec2 c; double r; dim_circle(d, e, c, r);
        return e.dt == DimType::Radius ? r : 2 * r;
    }
    Vec2 p1, p2; dim_points(d, e, p1, p2);
    return std::fabs(dot(p2 - p1, dim_direction(e, p1, p2)));
}

static std::string dim_text(const Doc& d, const Entity& e, double value) {
    std::string num = format_number(value, d.dimstyle.prec);
    if (e.dt == DimType::Radius) num = "R" + num;
    if (e.dt == DimType::Diameter) num = "\xC3\x98" + num; // U+00D8 Ø
    if (e.override_text.empty()) return num;
    std::string s = e.override_text;
    size_t pos = s.find("<>");
    if (pos != std::string::npos) s.replace(pos, 2, num);
    return s;
}

// Keep text readable: rotation in (-90, 90]
static double readable(double ang) {
    ang = norm_angle(ang);
    if (ang > PI / 2 + 1e-9 && ang <= 3 * PI / 2 + 1e-9) ang -= PI;
    return ang;
}

DimGeom dim_geom(const Doc& d, const Entity& e) {
    const DimStyle& st = d.dimstyle;
    const double k = st.scale;
    DimGeom g;
    g.text_h = st.txt * k;
    double asz = st.asz * k, exo = st.exo * k, exe = st.exe * k, gap = st.gap * k;

    if (e.dt == DimType::Linear || e.dt == DimType::Aligned) {
        Vec2 p1, p2; dim_points(d, e, p1, p2);
        Vec2 u = dim_direction(e, p1, p2), n = perp(u);
        Vec2 base = p1 + n * e.off;
        Vec2 d1 = base + u * dot(p1 - base, u);
        Vec2 d2 = base + u * dot(p2 - base, u);
        g.value = dist(d1, d2);
        for (auto [p, q] : {std::pair{p1, d1}, std::pair{p2, d2}}) {
            Vec2 v = q - p;
            double l = len(v);
            if (l < EPS) continue;
            Vec2 vn = v / l;
            if (l > exo) g.lines.push_back({p + vn * exo, q + vn * exe});
            else g.lines.push_back({q, q + vn * exe});
        }
        g.lines.push_back({d1, d2});
        if (g.value > EPS) {
            Vec2 ud = norm(d2 - d1);
            if (g.value >= 2.2 * asz) {
                g.arrows.push_back({d1, -ud});
                g.arrows.push_back({d2, ud});
            } else { // arrows outside
                g.arrows.push_back({d1, ud});
                g.arrows.push_back({d2, -ud});
                g.lines.push_back({d1 - ud * (asz * 2), d1});
                g.lines.push_back({d2, d2 + ud * (asz * 2)});
            }
        }
        g.text_rot = readable(angle_of(u));
        Vec2 up = perp(dir(g.text_rot));
        g.text_pos = (d1 + d2) * 0.5 + up * gap;
        g.grip = (d1 + d2) * 0.5;
    } else {
        Vec2 c; double r; dim_circle(d, e, c, r);
        g.value = e.dt == DimType::Radius ? r : 2 * r;
        Vec2 L = c + e.loc;
        Vec2 ud = norm(e.loc);
        Vec2 q = c + ud * r;
        bool outside = len(e.loc) > r;
        if (e.dt == DimType::Radius) {
            if (outside) { g.lines.push_back({q, L}); g.arrows.push_back({q, -ud}); }
            else { g.lines.push_back({c, q}); g.arrows.push_back({q, ud}); }
        } else {
            Vec2 q2 = c - ud * r;
            g.lines.push_back({q2, outside ? L : q});
            g.arrows.push_back({q, ud});
            g.arrows.push_back({q2, -ud});
        }
        g.text_rot = readable(angle_of(ud));
        Vec2 up = perp(dir(g.text_rot));
        g.text_pos = L + up * gap;
        g.grip = L;
    }
    g.text = dim_text(d, e, g.value);
    return g;
}

static std::vector<Vec2> dim_text_box(const DimGeom& g) {
    double w = text_width(g.text, g.text_h);
    Vec2 u = dir(g.text_rot), up = perp(u);
    Vec2 bl = g.text_pos - u * (w / 2);
    return {bl, bl + u * w, bl + u * w + up * g.text_h, bl + up * g.text_h};
}

// ---------------------------------------------------------------------------
// Key points & grips
// ---------------------------------------------------------------------------
bool key_point(const Doc& d, const Entity& e, PKey k, Vec2& out) {
    switch (e.type) {
    case EType::Line:
        if (k.kind == KeyKind::Vertex && k.idx >= 0 && k.idx < 2) { out = e.v[k.idx].p; return true; }
        if (k.kind == KeyKind::Mid) { out = (e.v[0].p + e.v[1].p) * 0.5; return true; }
        return false;
    case EType::Polyline: {
        int n = (int)e.v.size();
        if (k.kind == KeyKind::Vertex && k.idx >= 0 && k.idx < n) { out = e.v[k.idx].p; return true; }
        if (k.kind == KeyKind::Mid) {
            auto segs = entity_segs(e);
            if (k.idx >= 0 && k.idx < (int)segs.size()) { out = segs[k.idx].at(0.5); return true; }
        }
        if (k.kind == KeyKind::Center) {
            auto segs = entity_segs(e);
            if (k.idx >= 0 && k.idx < (int)segs.size() && segs[k.idx].arc) { out = segs[k.idx].c; return true; }
        }
        return false;
    }
    case EType::Circle:
        if (k.kind == KeyKind::Center) { out = e.c; return true; }
        if (k.kind == KeyKind::Quad) { out = e.c + dir(k.idx * PI / 2) * e.r; return true; }
        return false;
    case EType::Arc: {
        Seg s = entity_segs(e)[0];
        if (k.kind == KeyKind::Vertex) { out = k.idx == 0 ? s.a : s.b; return true; }
        if (k.kind == KeyKind::Mid) { out = s.at(0.5); return true; }
        if (k.kind == KeyKind::Center) { out = e.c; return true; }
        if (k.kind == KeyKind::Quad) { out = e.c + dir(k.idx * PI / 2) * e.r; return true; }
        return false;
    }
    case EType::Ellipse: {
        if (k.kind == KeyKind::Center) { out = e.c; return true; }
        if (k.kind == KeyKind::Quad) {
            Vec2 mi = perp(e.major) * e.ratio;
            Vec2 q[4] = {e.major, mi, -e.major, -mi};
            out = e.c + q[k.idx & 3];
            return true;
        }
        return false;
    }
    case EType::Text:
    case EType::MText:
        if (k.kind == KeyKind::Vertex && k.idx == 0) { out = e.v[0].p; return true; }
        return false;
    case EType::Dim: {
        if (k.kind == KeyKind::DimLine) { out = dim_geom(d, e).grip; return true; }
        if (e.dt == DimType::Linear || e.dt == DimType::Aligned) {
            Vec2 p1, p2; dim_points(d, e, p1, p2);
            if (k.kind == KeyKind::Vertex) { out = k.idx == 0 ? p1 : p2; return true; }
        }
        return false;
    }
    case EType::Hatch: return false;
    }
    return false;
}

std::vector<std::pair<PKey, Vec2>> grips(const Doc& d, const Entity& e) {
    std::vector<PKey> keys;
    switch (e.type) {
    case EType::Line: keys = {{KeyKind::Vertex, 0}, {KeyKind::Mid, 0}, {KeyKind::Vertex, 1}}; break;
    case EType::Polyline: {
        int n = (int)e.v.size(), segs = e.closed ? n : n - 1;
        for (int i = 0; i < n; i++) keys.push_back({KeyKind::Vertex, i});
        for (int i = 0; i < segs; i++) keys.push_back({KeyKind::Mid, i});
        break;
    }
    case EType::Circle: keys = {{KeyKind::Center, 0}, {KeyKind::Quad, 0}, {KeyKind::Quad, 1}, {KeyKind::Quad, 2}, {KeyKind::Quad, 3}}; break;
    case EType::Arc: keys = {{KeyKind::Vertex, 0}, {KeyKind::Mid, 0}, {KeyKind::Vertex, 1}, {KeyKind::Center, 0}}; break;
    case EType::Ellipse: keys = {{KeyKind::Center, 0}, {KeyKind::Quad, 0}, {KeyKind::Quad, 1}, {KeyKind::Quad, 2}, {KeyKind::Quad, 3}}; break;
    case EType::Text: case EType::MText: keys = {{KeyKind::Vertex, 0}}; break;
    case EType::Dim:
        if (e.dt == DimType::Linear || e.dt == DimType::Aligned) keys = {{KeyKind::Vertex, 0}, {KeyKind::Vertex, 1}};
        keys.push_back({KeyKind::DimLine, 0});
        break;
    case EType::Hatch: break;
    }
    std::vector<std::pair<PKey, Vec2>> out;
    for (auto k : keys) { Vec2 p; if (key_point(d, e, k, p)) out.push_back({k, p}); }
    return out;
}

void move_key(const Doc& d, Entity& e, PKey k, Vec2 np) {
    Vec2 cur;
    if (!key_point(d, e, k, cur)) return;
    Vec2 delta = np - cur;
    switch (e.type) {
    case EType::Line:
        if (k.kind == KeyKind::Vertex) e.v[k.idx].p = np;
        else transform(e, Xform::translate(delta));
        break;
    case EType::Polyline:
        if (k.kind == KeyKind::Vertex) e.v[k.idx].p = np;
        else if (k.kind == KeyKind::Mid) {
            int n = (int)e.v.size();
            e.v[k.idx].p += delta;
            e.v[(k.idx + 1) % n].p += delta;
        }
        break;
    case EType::Circle:
        if (k.kind == KeyKind::Center) e.c = np;
        else e.r = std::max(dist(e.c, np), 1e-9);
        break;
    case EType::Arc: {
        if (k.kind == KeyKind::Center) { e.c = np; break; }
        Seg s = entity_segs(e)[0];
        Vec2 a = s.a, m = s.at(0.5), b = s.b;
        if (k.kind == KeyKind::Vertex && k.idx == 0) a = np;
        else if (k.kind == KeyKind::Vertex) b = np;
        else if (k.kind == KeyKind::Mid) m = np;
        else { e.r = std::max(dist(e.c, np), 1e-9); break; }
        Seg ns;
        if (arc_3p(a, m, b, ns)) { Entity ne = make_arc_from_seg(ns); e.c = ne.c; e.r = ne.r; e.a0 = ne.a0; e.a1 = ne.a1; }
        break;
    }
    case EType::Ellipse:
        if (k.kind == KeyKind::Center) e.c = np;
        else if (k.idx == 0 || k.idx == 2) {
            double minor = len(e.major) * e.ratio;
            e.major = (k.idx == 0 ? np - e.c : e.c - np);
            double maj = len(e.major);
            if (maj > EPS) e.ratio = minor / maj;
            if (e.ratio > 1) { e = make_ellipse(e.c, e.major, e.ratio); }
        } else {
            double maj = len(e.major);
            if (maj > EPS) e.ratio = std::fabs(dot(np - e.c, norm(perp(e.major)))) / maj;
            if (e.ratio > 1) { Entity ne = make_ellipse(e.c, e.major, e.ratio); e.major = ne.major; e.ratio = ne.ratio; }
            e.ratio = std::max(e.ratio, 1e-6);
        }
        break;
    case EType::Text: case EType::MText:
        e.v[0].p = np;
        break;
    case EType::Dim:
        if (k.kind == KeyKind::DimLine) {
            if (e.dt == DimType::Linear || e.dt == DimType::Aligned) {
                Vec2 p1, p2; dim_points(d, e, p1, p2);
                e.off = dot(np - p1, perp(dim_direction(e, p1, p2)));
            } else {
                Vec2 c; double r; dim_circle(d, e, c, r);
                e.loc = np - c;
            }
        } else if (k.kind == KeyKind::Vertex) {
            Vec2 p1, p2; dim_points(d, e, p1, p2);
            Vec2 line_pt = p1 + perp(dim_direction(e, p1, p2)) * e.off;
            if (k.idx == 0) { e.p1 = np; e.r1 = {}; } else { e.p2 = np; e.r2 = {}; }
            dim_points(d, e, p1, p2);
            e.off = dot(line_pt - p1, perp(dim_direction(e, p1, p2)));
        }
        break;
    case EType::Hatch: break;
    }
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------
void transform(Entity& e, const Xform& x) {
    double s = x.scale();
    switch (e.type) {
    case EType::Line:
    case EType::Polyline:
        for (auto& v : e.v) { v.p = x.apply(v.p); if (x.mirrored()) v.bulge = -v.bulge; }
        break;
    case EType::Circle:
        e.c = x.apply(e.c); e.r *= s;
        break;
    case EType::Arc: {
        Seg sg = entity_segs(e)[0];
        Vec2 a = x.apply(sg.a), b = x.apply(sg.b);
        e.c = x.apply(e.c); e.r *= s;
        if (x.mirrored()) std::swap(a, b);
        e.a0 = norm_angle(angle_of(a - e.c));
        e.a1 = norm_angle(angle_of(b - e.c));
        break;
    }
    case EType::Ellipse:
        e.c = x.apply(e.c); e.major = x.apply_vec(e.major);
        break;
    case EType::Text:
    case EType::MText: {
        if (!x.mirrored()) {
            e.v[0].p = x.apply(e.v[0].p);
            e.rot = norm_angle(e.rot + x.rotation());
            e.height *= s; e.width *= s;
        } else { // MIRRTEXT = 0: keep the text readable, mirror its placement only
            auto box = text_box(e);
            e.height *= s; e.width *= s;
            Vec2 u = dir(e.rot), w = perp(u);
            double mnU = 1e300, mnW = 1e300, mxW = -1e300;
            for (auto& p : box) {
                Vec2 q = x.apply(p);
                mnU = std::min(mnU, dot(q, u)); mnW = std::min(mnW, dot(q, w)); mxW = std::max(mxW, dot(q, w));
            }
            if (e.type == EType::Text) mnW += 0.25 * e.height; // undo descender allowance
            e.v[0].p = u * mnU + w * (e.type == EType::Text ? mnW : mxW);
        }
        break;
    }
    case EType::Hatch:
        for (auto& l : e.loops) for (auto& p : l) p = x.apply(p);
        e.seed = x.apply(e.seed);
        e.pscale *= s;
        e.pangle = norm_angle(e.pangle + x.rotation());
        break;
    case EType::Dim: {
        if (e.dt == DimType::Linear || e.dt == DimType::Aligned) {
            Vec2 u = dim_direction(e, e.p1, e.p2);
            Vec2 lp = x.apply(e.p1 + perp(u) * e.off);
            e.p1 = x.apply(e.p1); e.p2 = x.apply(e.p2);
            e.off = dot(lp - e.p1, perp(dim_direction(e, e.p1, e.p2)));
        } else {
            e.c = x.apply(e.c); e.r *= s;
            e.loc = x.apply_vec(e.loc);
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Bounds & hit testing
// ---------------------------------------------------------------------------
static std::vector<std::vector<Vec2>> outline_polys(const Doc& d, const Entity& e) {
    std::vector<std::vector<Vec2>> out;
    switch (e.type) {
    case EType::Line: case EType::Arc: case EType::Circle: case EType::Polyline: {
        std::vector<Vec2> p;
        auto segs = entity_segs(e);
        for (size_t i = 0; i < segs.size(); i++) seg_tessellate(segs[i], p, 96, i + 1 == segs.size());
        out.push_back(std::move(p));
        break;
    }
    case EType::Ellipse: {
        auto p = ellipse_points(e, 96); p.push_back(p[0]); out.push_back(std::move(p));
        break;
    }
    case EType::Text: case EType::MText: {
        auto b = text_box(e); b.push_back(b[0]); out.push_back(std::move(b));
        break;
    }
    case EType::Hatch:
        for (auto l : e.loops) { if (!l.empty()) l.push_back(l[0]); out.push_back(std::move(l)); }
        break;
    case EType::Dim: {
        DimGeom g = dim_geom(d, e);
        for (auto& [a, b] : g.lines) out.push_back({a, b});
        auto tb = dim_text_box(g); tb.push_back(tb[0]); out.push_back(std::move(tb));
        break;
    }
    }
    return out;
}

BBox bbox(const Doc& d, const Entity& e) {
    BBox b;
    if (e.type == EType::Circle) { b.add(e.c - Vec2{e.r, e.r}); b.add(e.c + Vec2{e.r, e.r}); return b; }
    for (auto& poly : outline_polys(d, e)) for (auto& p : poly) b.add(p);
    return b;
}

static bool inside_loops(const std::vector<std::vector<Vec2>>& loops, Vec2 p) {
    bool in = false;
    for (auto& l : loops) if (l.size() >= 3 && point_in_polygon(l, p)) in = !in;
    return in;
}

double hit_dist(const Doc& d, const Entity& e, Vec2 p, double tol) {
    BBox b = bbox(d, e);
    if (b.valid()) {
        BBox bt = b; bt.mn -= Vec2{tol, tol}; bt.mx += Vec2{tol, tol};
        if (!bt.contains(p)) return 1e300;
    }
    if (is_curve(e) && e.type != EType::Ellipse) {
        double best = 1e300;
        for (auto& s : entity_segs(e)) best = std::min(best, seg_dist(s, p));
        return best;
    }
    if (e.type == EType::Text || e.type == EType::MText) {
        auto box = text_box(e);
        if (point_in_polygon(box, p)) return 0;
    }
    if (e.type == EType::Hatch && inside_loops(e.loops, p)) return 0;
    double best = 1e300;
    auto polys = outline_polys(d, e);
    if (e.type == EType::Dim && !polys.empty() && point_in_polygon(polys.back(), p)) return 0;
    for (auto& poly : polys)
        for (size_t i = 0; i + 1 < poly.size(); i++) best = std::min(best, seg_point_dist(poly[i], poly[i + 1], p));
    return best;
}

bool inside_window(const Doc& d, const Entity& e, const BBox& w) { return w.contains(bbox(d, e)); }

bool crosses_window(const Doc& d, const Entity& e, const BBox& w) {
    BBox b = bbox(d, e);
    if (!b.valid() || !w.overlaps(b)) return false;
    if (w.contains(b)) return true;
    Vec2 c[4] = {w.mn, {w.mx.x, w.mn.y}, w.mx, {w.mn.x, w.mx.y}};
    for (auto& poly : outline_polys(d, e)) {
        for (size_t i = 0; i < poly.size(); i++) {
            if (w.contains(poly[i])) return true;
            if (i + 1 < poly.size())
                for (int k = 0; k < 4; k++) if (segments_cross(poly[i], poly[i + 1], c[k], c[(k + 1) % 4])) return true;
        }
    }
    if ((e.type == EType::Text || e.type == EType::MText) && point_in_polygon(text_box(e), w.center())) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Dimension driving
// ---------------------------------------------------------------------------
static bool vertex_based(const Entity& e) { return e.type == EType::Line || e.type == EType::Polyline; }

// Entities connected to `start` through coincident vertices (lines/polylines only).
static std::vector<uint32_t> connected_component(const Doc& d, uint32_t start, double tol) {
    std::vector<uint32_t> out{start};
    std::set<uint32_t> seen{start};
    for (size_t i = 0; i < out.size(); i++) {
        const Entity* a = d.find(out[i]);
        if (!a || !vertex_based(*a)) continue;
        for (auto& b : d.ents) {
            if (seen.count(b.id) || !vertex_based(b)) continue;
            bool touch = false;
            for (auto& va : a->v) { for (auto& vb : b.v) if (near(va.p, vb.p, tol)) { touch = true; break; } if (touch) break; }
            if (touch) { seen.insert(b.id); out.push_back(b.id); }
        }
    }
    return out;
}

bool drive_dim(Doc& d, uint32_t dim_id, double nv, bool scale) {
    Entity* dim = d.find(dim_id);
    if (!dim || dim->type != EType::Dim || nv <= 0) return false;

    if (dim->dt == DimType::Radius || dim->dt == DimType::Diameter) {
        double r = dim->dt == DimType::Radius ? nv : nv / 2;
        Entity* t = dim->rent ? d.find(dim->rent) : nullptr;
        if (t) t->r = r;
        dim = d.find(dim_id);
        dim->r = r;
        d.touch();
        update_associative(d);
        return true;
    }

    Vec2 p1, p2; dim_points(d, *dim, p1, p2);
    Vec2 u = dim_direction(*dim, p1, p2);
    double proj = dot(p2 - p1, u);
    double cur = std::fabs(proj);
    double sgn = proj >= 0 ? 1.0 : -1.0;
    Vec2 us = u * sgn;
    PointRef r1 = dim->r1, r2 = dim->r2;

    if (scale) {
        if (cur < EPS) return false;
        Xform x = Xform::scale_about(p1, nv / cur);
        std::set<uint32_t> ids;
        if (r1.valid()) ids.insert(r1.ent);
        if (r2.valid()) ids.insert(r2.ent);
        for (auto id : ids) if (auto* t = d.find(id)) transform(*t, x);
        if (!r2.valid()) dim->p2 = x.apply(dim->p2);
        d.touch();
        update_associative(d);
        return true;
    }

    Vec2 delta = us * (nv - cur);
    double tol = 1e-6 * std::max(1.0, cur);
    if (!r2.valid()) {
        dim->p2 = dim->p2 + delta;
        d.touch();
        return true;
    }
    Entity* e2 = d.find(r2.ent);
    if (!e2) return false;
    if (!vertex_based(*e2)) {
        transform(*e2, Xform::translate(delta));
    } else {
        // Stretch: every vertex of the connected geometry lying on the p2 side of the midpoint moves.
        double half = cur / 2;
        for (auto id : connected_component(d, r2.ent, tol)) {
            Entity* t = d.find(id);
            for (auto& v : t->v)
                if (dot(v.p - p1, us) > half - tol) v.p += delta;
        }
    }
    d.touch();
    update_associative(d);
    return true;
}

// ---------------------------------------------------------------------------
// Hatch boundary detection (planar face walk)
// ---------------------------------------------------------------------------
bool loops_from_entities(const Doc& d, const std::vector<uint32_t>& ids, std::vector<std::vector<Vec2>>& loops) {
    std::vector<std::vector<Vec2>> out;
    for (auto id : ids) {
        const Entity* e = d.find(id);
        if (!e || !is_closed_curve(*e)) return false;
        out.push_back(closed_loop(*e, 128));
    }
    loops = std::move(out);
    return !loops.empty();
}

bool find_boundary(const Doc& d, Vec2 seed, std::vector<std::vector<Vec2>>& loops) {
    constexpr int STEPS = 72;
    struct L { Vec2 a, b; BBox bb; };
    std::vector<L> segs;
    BBox all;
    for (auto& e : d.ents) {
        if (!is_curve(e)) continue;
        std::vector<Vec2> pts;
        bool closed = is_closed_curve(e);
        if (e.type == EType::Ellipse) pts = ellipse_points(e, STEPS);
        else { auto ss = entity_segs(e); for (size_t i = 0; i < ss.size(); i++) seg_tessellate(ss[i], pts, STEPS, i + 1 == ss.size() && !closed); }
        size_t n = pts.size();
        size_t cnt = closed ? n : n - 1;
        for (size_t i = 0; i < cnt && n >= 2; i++) {
            L l{pts[i], pts[(i + 1) % n], {}};
            l.bb.add(l.a); l.bb.add(l.b); all.add(l.bb);
            if (!near(l.a, l.b, 1e-12)) segs.push_back(l);
        }
    }
    if (segs.empty() || !all.contains(seed)) return false;
    double scale = std::max(all.mx.x - all.mn.x, all.mx.y - all.mn.y);
    double tol = 1e-7 * std::max(1.0, scale);
    seed.y += 1.2345e-7 * std::max(1.0, scale); // avoid ray passing exactly through vertices

    // Split segments at intersections
    size_t n = segs.size();
    std::vector<std::vector<double>> cuts(n, std::vector<double>{0.0, 1.0});
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            BBox bi = segs[i].bb; bi.mn -= Vec2{tol, tol}; bi.mx += Vec2{tol, tol};
            if (!bi.overlaps(segs[j].bb)) continue;
            double t, u;
            if (line_line(segs[i].a, segs[i].b, segs[j].a, segs[j].b, t, u) && t > -1e-9 && t < 1 + 1e-9 && u > -1e-9 && u < 1 + 1e-9) {
                cuts[i].push_back(std::clamp(t, 0.0, 1.0));
                cuts[j].push_back(std::clamp(u, 0.0, 1.0));
            }
        }

    // Vertex merging via spatial hash
    std::vector<Vec2> verts;
    std::unordered_map<long long, std::vector<int>> grid;
    double cell = tol * 16;
    auto key = [&](long long ix, long long iy) { return ix * 73856093LL ^ iy * 19349663LL; };
    auto vid = [&](Vec2 p) {
        long long ix = (long long)std::floor(p.x / cell), iy = (long long)std::floor(p.y / cell);
        for (long long dx = -1; dx <= 1; dx++)
            for (long long dy = -1; dy <= 1; dy++) {
                auto it = grid.find(key(ix + dx, iy + dy));
                if (it == grid.end()) continue;
                for (int v : it->second) if (near(verts[v], p, tol * 4)) return v;
            }
        verts.push_back(p);
        grid[key(ix, iy)].push_back((int)verts.size() - 1);
        return (int)verts.size() - 1;
    };

    std::set<std::pair<int, int>> edge_set;
    for (size_t i = 0; i < n; i++) {
        auto& c = cuts[i];
        std::sort(c.begin(), c.end());
        int prev = vid(lerp(segs[i].a, segs[i].b, c[0]));
        for (size_t k = 1; k < c.size(); k++) {
            int cur = vid(lerp(segs[i].a, segs[i].b, c[k]));
            if (cur != prev) edge_set.insert({std::min(prev, cur), std::max(prev, cur)});
            prev = cur;
        }
    }
    if (edge_set.empty()) return false;

    // Adjacency sorted by angle
    std::vector<std::vector<int>> adj(verts.size());
    for (auto [a, b] : edge_set) { adj[a].push_back(b); adj[b].push_back(a); }
    for (size_t v = 0; v < adj.size(); v++)
        std::sort(adj[v].begin(), adj[v].end(), [&](int x, int y) {
            return angle_of(verts[x] - verts[v]) < angle_of(verts[y] - verts[v]);
        });

    auto walk = [&](int u, int v, std::vector<Vec2>& pts, std::set<std::pair<int, int>>& used) {
        int su = u, sv = v;
        pts.clear();
        for (int iter = 0; iter < 200000; iter++) {
            pts.push_back(verts[u]);
            used.insert({u, v});
            auto& nb = adj[v];
            int k = (int)nb.size();
            int j = (int)(std::find(nb.begin(), nb.end(), u) - nb.begin());
            int w = nb[(j - 1 + k) % k];
            u = v; v = w;
            if (u == su && v == sv) return true;
        }
        return false;
    };

    // Cast a ray to +X and inspect crossed edges in order of distance
    std::vector<std::pair<double, std::pair<int, int>>> hits;
    for (auto [a, b] : edge_set) {
        Vec2 A = verts[a], B = verts[b];
        if ((A.y > seed.y) == (B.y > seed.y)) continue;
        double x = A.x + (seed.y - A.y) * (B.x - A.x) / (B.y - A.y);
        if (x > seed.x) hits.push_back({x, {a, b}});
    }
    std::sort(hits.begin(), hits.end());

    std::set<std::pair<int, int>> used;
    std::vector<std::vector<Vec2>> islands;
    std::vector<Vec2> outer, pts;
    for (auto& [x, e] : hits) {
        auto [a, b] = e;
        if (cross(verts[b] - verts[a], seed - verts[a]) < 0) std::swap(a, b);
        if (used.count({a, b})) continue;
        if (!walk(a, b, pts, used)) continue;
        double area = polygon_area(pts);
        if (area > 0 && point_in_polygon(pts, seed)) { outer = pts; break; }
        if (area < 0 && !point_in_polygon(pts, seed)) islands.push_back(pts);
    }
    if (outer.empty()) return false;

    // Islands: loops found along the ray, plus closed entities lying fully inside the boundary
    auto same_loop = [&](const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
        double aa = std::fabs(polygon_area(a)), ab = std::fabs(polygon_area(b));
        if (std::fabs(aa - ab) > 0.02 * std::max(aa, ab)) return false;
        BBox ba, bb; for (auto& p : a) ba.add(p); for (auto& p : b) bb.add(p);
        return near(ba.center(), bb.center(), 0.01 * std::max(1.0, len(ba.mx - ba.mn)));
    };
    loops.clear();
    loops.push_back(outer);
    for (auto& il : islands) loops.push_back(il);
    for (auto& e : d.ents) {
        if (!is_closed_curve(e)) continue;
        auto l = closed_loop(e, STEPS);
        if (l.size() < 3 || point_in_polygon(l, seed)) continue;
        bool inside = std::all_of(l.begin(), l.end(), [&](Vec2 p) { return point_in_polygon(outer, p); });
        if (!inside) continue;
        bool dup = same_loop(l, outer);
        for (auto& ex : loops) dup = dup || same_loop(l, ex);
        if (!dup) loops.push_back(l);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Associativity
// ---------------------------------------------------------------------------
void update_associative(Doc& d) {
    for (auto& e : d.ents) {
        if (e.type == EType::Dim) {
            if (e.dt == DimType::Linear || e.dt == DimType::Aligned) {
                if (e.r1.valid()) { auto* t = d.find(e.r1.ent); if (!t || !key_point(d, *t, e.r1.key, e.p1)) e.r1 = {}; }
                if (e.r2.valid()) { auto* t = d.find(e.r2.ent); if (!t || !key_point(d, *t, e.r2.key, e.p2)) e.r2 = {}; }
            } else if (e.rent) {
                auto* t = d.find(e.rent);
                if (t && (t->type == EType::Circle || t->type == EType::Arc)) { e.c = t->c; e.r = t->r; }
                else e.rent = 0;
            }
        }
    }
    for (auto& e : d.ents) {
        if (e.type != EType::Hatch) continue;
        if (!e.bnd.empty()) {
            std::vector<std::vector<Vec2>> l;
            if (loops_from_entities(d, e.bnd, l)) e.loops = std::move(l);
            else e.bnd.clear();
        } else if (e.has_seed) {
            std::vector<std::vector<Vec2>> l;
            if (find_boundary(d, e.seed, l)) e.loops = std::move(l);
        }
    }
}

// ---------------------------------------------------------------------------
// Segs -> entities (with merging of collinear lines / co-circular arcs)
// ---------------------------------------------------------------------------
static std::vector<Seg> merge_segs(const std::vector<Seg>& in) {
    std::vector<Seg> out;
    for (auto& s : in) {
        if (s.length() < 1e-12) continue;
        if (!out.empty()) {
            Seg& p = out.back();
            if (!p.arc && !s.arc && std::fabs(cross(norm(p.b - p.a), norm(s.b - s.a))) < 1e-9 && dot(p.b - p.a, s.b - s.a) > 0) {
                p = Seg::line(p.a, s.b);
                continue;
            }
            if (p.arc && s.arc && near(p.c, s.c, 1e-7 * std::max(1.0, p.r)) && std::fabs(p.r - s.r) < 1e-7 * std::max(1.0, p.r) && (p.sw > 0) == (s.sw > 0)) {
                Vec2 a = p.a, b = s.b;
                p = Seg::arc_of(p.c, p.r, p.a0, p.sw + s.sw);
                p.a = a; p.b = b;
                continue;
            }
        }
        out.push_back(s);
    }
    return out;
}

std::vector<Entity> entities_from_segs(const std::vector<Seg>& segs_in, bool closed, const Entity& proto) {
    std::vector<Seg> segs = merge_segs(segs_in);
    std::vector<Entity> out;
    if (segs.empty()) return out;
    Entity e;
    if (segs.size() == 1) {
        const Seg& s = segs[0];
        if (!s.arc) e = make_line(s.a, s.b);
        else if (closed && std::fabs(std::fabs(s.sw) - TAU) < 1e-6) e = make_circle(s.c, s.r);
        else e = make_arc_from_seg(s);
    } else {
        std::vector<Vtx> v;
        for (auto& s : segs) v.push_back({s.a, s.bulge()});
        if (!closed) v.push_back({segs.back().b, 0});
        e = make_polyline(v, closed);
    }
    e.color = proto.color;
    e.group = proto.group;
    out.push_back(std::move(e));
    return out;
}

// ---------------------------------------------------------------------------
// Trim / Extend
// ---------------------------------------------------------------------------
static std::vector<Seg> sub_chain(const std::vector<Seg>& S, double from, double to) {
    std::vector<Seg> out;
    int n = (int)S.size();
    for (int k = std::max(0, (int)std::floor(from)); k < n && k < std::ceil(to); k++) {
        double t0 = std::max(from - k, 0.0), t1 = std::min(to - k, 1.0);
        if (t1 - t0 > 1e-9) out.push_back(S[k].sub(t0, t1));
    }
    return out;
}

static double chain_param(const std::vector<Seg>& S, Vec2 p) {
    double best = 1e300, param = 0;
    for (size_t i = 0; i < S.size(); i++) {
        double t = seg_closest_t(S[i], p);
        double dd = dist(S[i].at(t), p);
        if (dd < best) { best = dd; param = i + t; }
    }
    return param;
}

TrimResult trim_entity(Doc& d, uint32_t id, Vec2 pick) {
    Entity* ep = d.find(id);
    if (!ep || !is_curve(*ep)) return TrimResult::Nothing;
    if (ep->type == EType::Ellipse) return TrimResult::Nothing;
    Entity e = *ep;
    auto S = entity_segs(e);
    int n = (int)S.size();
    bool closed = is_closed_curve(e);

    std::vector<double> cuts;
    for (auto& o : d.ents) {
        if (o.id == id || !is_curve(o)) continue;
        auto O = entity_segs(o);
        for (int i = 0; i < n; i++)
            for (auto& os : O) {
                std::vector<Hit> hits;
                seg_intersect(S[i], os, hits, 1e-9);
                for (auto& h : hits) cuts.push_back(i + std::clamp(h.t, 0.0, 1.0));
            }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) { return std::fabs(a - b) < 1e-9; }), cuts.end());
    if (closed) { // param n == param 0
        for (auto& c : cuts) if (c >= n - 1e-9) c = 0;
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) { return std::fabs(a - b) < 1e-9; }), cuts.end());
    }

    if (cuts.empty()) { d.erase(id); return TrimResult::Deleted; }

    double sp = chain_param(S, pick);
    std::vector<std::vector<Seg>> pieces;
    if (!closed) {
        double lo = -1, hi = -1;
        for (double c : cuts) { if (c < sp) lo = c; if (c > sp && hi < 0) hi = c; }
        if (lo >= 0) pieces.push_back(sub_chain(S, 0, lo));
        if (hi >= 0) pieces.push_back(sub_chain(S, hi, n));
    } else {
        if (cuts.size() < 2) return TrimResult::Nothing;
        double lo = cuts.back(), hi = cuts.front();
        for (double c : cuts) { if (c < sp) lo = c; }
        for (double c : cuts) { if (c > sp) { hi = c; break; } }
        std::vector<Seg> p;
        if (hi < lo) p = sub_chain(S, hi, lo);
        else { p = sub_chain(S, hi, n); auto b = sub_chain(S, 0, lo); p.insert(p.end(), b.begin(), b.end()); }
        pieces.push_back(p);
    }

    d.erase(id);
    for (auto& p : pieces) {
        if (p.empty()) continue;
        double total = 0; for (auto& s : p) total += s.length();
        if (total < 1e-9) continue;
        for (auto& ne : entities_from_segs(p, false, e)) d.add(ne);
    }
    return TrimResult::Trimmed;
}

bool extend_entity(Doc& d, uint32_t id, Vec2 pick) {
    Entity* ep = d.find(id);
    if (!ep) return false;
    if (ep->type != EType::Line && ep->type != EType::Arc && !(ep->type == EType::Polyline && !ep->closed)) return false;
    auto S = entity_segs(*ep);
    int n = (int)S.size();
    double sp = chain_param(S, pick);
    bool at_start = sp < n * 0.5;
    Seg s = at_start ? S.front().reversed() : S.back();

    BBox all;
    for (auto& o : d.ents) all.add(bbox(d, o));
    double big = std::max(1e4, 10 * len(all.mx - all.mn));

    Seg probe;
    double rem = 0;
    if (!s.arc) probe = Seg::line(s.b, s.b + s.tangent(1) * big);
    else {
        rem = TAU - std::fabs(s.sw) - 1e-9;
        if (rem <= 1e-9) return false;
        probe = Seg::arc_of(s.c, s.r, s.a0 + s.sw, s.sw > 0 ? rem : -rem);
    }
    double best = 1e300;
    for (auto& o : d.ents) {
        if (o.id == id || !is_curve(o)) continue;
        for (auto& os : entity_segs(o)) {
            std::vector<Hit> hits;
            seg_intersect(probe, os, hits, 1e-12);
            for (auto& h : hits) if (h.t > 1e-9 && h.t < best) best = h.t;
        }
    }
    if (best > 1.0) return false;
    Vec2 P = probe.at(best);
    Entity& e = *ep;
    double newsw = s.arc ? s.sw + (s.sw > 0 ? 1 : -1) * best * rem : 0;
    switch (e.type) {
    case EType::Line: e.v[at_start ? 0 : 1].p = P; break;
    case EType::Arc:
        if (at_start) e.a0 = norm_angle(e.a0 - best * rem);
        else e.a1 = norm_angle(e.a1 + best * rem);
        break;
    case EType::Polyline: {
        int m = (int)e.v.size();
        if (at_start) {
            e.v[0].p = P;
            if (s.arc) e.v[0].bulge = std::tan(-newsw / 4);
        } else {
            e.v[m - 1].p = P;
            if (s.arc) e.v[m - 2].bulge = std::tan(newsw / 4);
        }
        break;
    }
    default: break;
    }
    d.touch();
    return true;
}

// ---------------------------------------------------------------------------
// Join / Explode
// ---------------------------------------------------------------------------
int join_entities(Doc& d, const std::vector<uint32_t>& ids, std::vector<uint32_t>& created) {
    struct Piece { std::vector<Seg> segs; std::vector<uint32_t> src; bool absorbed = false; };
    std::vector<Piece> pieces;
    BBox all;
    for (auto id : ids) {
        const Entity* e = d.find(id);
        if (!e) continue;
        if (e->type == EType::Line || e->type == EType::Arc || (e->type == EType::Polyline && !e->closed)) {
            pieces.push_back({entity_segs(*e), {id}});
            all.add(bbox(d, *e));
        }
    }
    if (pieces.size() < 2) return 0;
    double tol = 1e-6 * std::max(1.0, len(all.mx - all.mn));
    auto single_line = [](const Piece& p) { return !p.absorbed && p.segs.size() == 1 && !p.segs[0].arc; };

    // Collinear lines join even across gaps
    for (size_t i = 0; i < pieces.size(); i++) {
        if (!single_line(pieces[i])) continue;
        for (size_t j = 0; j < pieces.size(); j++) {
            if (i == j || !single_line(pieces[j])) continue;
            Seg& a = pieces[i].segs[0];
            const Seg& b = pieces[j].segs[0];
            Vec2 u = norm(a.b - a.a);
            if (std::fabs(cross(u, norm(b.b - b.a))) > 1e-9 || std::fabs(cross(u, b.a - a.a)) > tol) continue;
            double t[4] = {0, dot(a.b - a.a, u), dot(b.a - a.a, u), dot(b.b - a.a, u)};
            double mn = *std::min_element(t, t + 4), mx = *std::max_element(t, t + 4);
            a = Seg::line(a.a + u * mn, a.a + u * mx);
            pieces[i].src.insert(pieces[i].src.end(), pieces[j].src.begin(), pieces[j].src.end());
            pieces[j].absorbed = true;
        }
    }

    // Chain pieces whose endpoints coincide
    std::vector<bool> taken(pieces.size());
    for (size_t i = 0; i < pieces.size(); i++) taken[i] = pieces[i].absorbed;
    int joined = 0;
    for (size_t i = 0; i < pieces.size(); i++) {
        if (taken[i]) continue;
        taken[i] = true;
        std::vector<Seg> chain = pieces[i].segs;
        std::vector<uint32_t> src = pieces[i].src;
        for (bool grew = true; grew;) {
            grew = false;
            for (size_t j = 0; j < pieces.size(); j++) {
                if (taken[j]) continue;
                const auto& ps = pieces[j].segs;
                std::vector<Seg> rev;
                for (auto it = ps.rbegin(); it != ps.rend(); ++it) rev.push_back(it->reversed());
                Vec2 cs = chain.front().a, ce = chain.back().b;
                if (near(ce, ps.front().a, tol)) chain.insert(chain.end(), ps.begin(), ps.end());
                else if (near(ce, ps.back().b, tol)) chain.insert(chain.end(), rev.begin(), rev.end());
                else if (near(cs, ps.back().b, tol)) chain.insert(chain.begin(), ps.begin(), ps.end());
                else if (near(cs, ps.front().a, tol)) chain.insert(chain.begin(), rev.begin(), rev.end());
                else continue;
                taken[j] = true;
                src.insert(src.end(), pieces[j].src.begin(), pieces[j].src.end());
                grew = true;
            }
        }
        if (src.size() < 2) continue; // nothing joined to this piece
        bool closed = chain.size() > 1 && near(chain.front().a, chain.back().b, tol);
        if (closed) chain.back().b = chain.front().a;
        Entity proto = *d.find(src[0]);
        for (auto id : src) d.erase(id);
        for (auto& ne : entities_from_segs(chain, closed, proto)) created.push_back(d.add(ne));
        joined += (int)src.size();
    }
    return joined;
}

int explode_entities(Doc& d, const std::vector<uint32_t>& ids) {
    int count = 0;
    for (auto id : ids) {
        const Entity* e = d.find(id);
        if (!e || e->type != EType::Polyline) continue;
        Entity proto = *e;
        auto segs = entity_segs(*e);
        d.erase(id);
        for (auto& s : segs) for (auto& ne : entities_from_segs({s}, false, proto)) d.add(ne);
        count++;
    }
    return count;
}

} // namespace cad
