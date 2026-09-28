#include "cad.h"

#include <map>
#include <set>

namespace cad {

using Ents = std::vector<Entity>;

static std::string fmt(double v) { return format_number(v, 4); }
static double rad(double d) { return d * PI / 180.0; }

// Copies of entities for previews: associativity stripped so they draw from their own data.
static Ents ghosts(const Cad& c, const std::vector<uint32_t>& ids, const Xform& x) {
    Ents out;
    for (auto id : ids) {
        const Entity* e = c.doc.find(id);
        if (!e) continue;
        Entity g = *e;
        if (g.type == EType::Dim) {
            dim_points(c.doc, *e, g.p1, g.p2);
            dim_circle(c.doc, *e, g.c, g.r);
            g.r1 = {}; g.r2 = {}; g.rent = 0;
        }
        transform(g, x);
        out.push_back(std::move(g));
    }
    return out;
}

static void apply_xform(Cad& c, const std::vector<uint32_t>& ids, const Xform& x) {
    std::set<uint32_t> set(ids.begin(), ids.end());
    bool has_curve = false;
    for (auto id : ids) if (auto* e = c.doc.find(id); e && is_curve(*e)) has_curve = true;
    for (auto id : ids) {
        Entity* e = c.doc.find(id);
        if (!e) continue;
        if (e->type == EType::Hatch) {
            for (auto b : e->bnd) if (!set.count(b)) { e->bnd.clear(); break; }
            if (!has_curve) e->has_seed = false;
        }
        transform(*e, x);
    }
    c.doc.touch();
    update_associative(c.doc);
}

static std::vector<uint32_t> copy_entities(Cad& c, const std::vector<uint32_t>& ids, const Xform& x) {
    std::map<uint32_t, uint32_t> id_map;
    std::map<uint32_t, uint32_t> group_map;
    std::vector<uint32_t> created;
    Ents copies;
    for (auto id : ids) {
        const Entity* e = c.doc.find(id);
        if (!e) continue;
        Entity n = *e;
        if (n.group) {
            if (!group_map.count(n.group)) group_map[n.group] = c.doc.next_group++;
            n.group = group_map[n.group];
        }
        transform(n, x);
        copies.push_back(n);
    }
    for (auto& n : copies) {
        uint32_t old = n.id;
        uint32_t nid = c.doc.add(n);
        id_map[old] = nid;
        created.push_back(nid);
    }
    auto remap = [&](uint32_t id) -> uint32_t { auto it = id_map.find(id); return it == id_map.end() ? 0 : it->second; };
    for (auto nid : created) {
        Entity* e = c.doc.find(nid);
        if (e->type == EType::Dim) {
            if (e->r1.valid()) { e->r1.ent = remap(e->r1.ent); if (!e->r1.ent) e->r1 = {}; }
            if (e->r2.valid()) { e->r2.ent = remap(e->r2.ent); if (!e->r2.ent) e->r2 = {}; }
            if (e->rent) e->rent = remap(e->rent);
        }
        if (e->type == EType::Hatch) {
            for (auto& b : e->bnd) b = remap(b);
            if (std::find(e->bnd.begin(), e->bnd.end(), 0u) != e->bnd.end()) e->bnd.clear();
        }
    }
    c.doc.touch();
    update_associative(c.doc);
    return created;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
static Task<> cmd_line(Cad& c) {
    Input in = co_await c.get_point("Specify first point:");
    if (!in.ok()) co_return;
    std::vector<Vec2> pts{in.p};
    std::vector<uint32_t> made;
    for (;;) {
        Vec2 prev = pts.back();
        std::string pr = made.size() >= 2 ? "Specify next point or [Close/Undo]:" : "Specify next point or [Undo]:";
        in = co_await c.get_point(pr, prev, [&c, prev](Vec2 p) { return Ents{c.styled(make_line(prev, p))}; }, false);
        if (in.st == Input::None) break;
        if (in.kw("Undo")) {
            if (made.empty()) { c.print("All segments already undone."); continue; }
            c.doc.erase(made.back());
            made.pop_back();
            pts.pop_back();
            continue;
        }
        if (in.kw("Close")) {
            c.checkpoint();
            made.push_back(c.doc.add(c.styled(make_line(prev, pts.front()))));
            break;
        }
        if (in.ok() && !near(in.p, prev, 1e-12)) {
            c.checkpoint();
            made.push_back(c.doc.add(c.styled(make_line(prev, in.p))));
            pts.push_back(in.p);
        }
    }
}

static Task<> cmd_pline(Cad& c) {
    Input in = co_await c.get_point("Specify start point:");
    if (!in.ok()) co_return;
    c.print("Current line-width is 0.0000");
    std::vector<Vtx> v{{in.p, 0}};
    bool closed = false;
    for (;;) {
        Vec2 prev = v.back().p;
        auto pv = [&c, &v](Vec2 p) { auto vv = v; vv.push_back({p, 0}); return Ents{c.styled(make_polyline(vv, false))}; };
        in = co_await c.get_point(v.size() >= 3 ? "Specify next point or [Close/Undo]:" : "Specify next point or [Undo]:", prev, pv, false);
        if (in.st == Input::None) break;
        if (in.kw("Undo")) { if (v.size() > 1) v.pop_back(); continue; }
        if (in.kw("Close")) { closed = true; break; }
        if (in.ok() && !near(in.p, prev, 1e-12)) v.push_back({in.p, 0});
    }
    if (v.size() >= 2) {
        c.checkpoint();
        c.doc.add(c.styled(make_polyline(v, closed && v.size() >= 3)));
    }
}

static Task<> cmd_circle(Cad& c) {
    Input in = co_await c.get_point("Specify center point for circle or [3P/2P]:");
    if (in.kw("3P")) {
        Input a = co_await c.get_point("Specify first point on circle:");
        if (!a.ok()) co_return;
        Input b = co_await c.get_point("Specify second point on circle:", a.p);
        if (!b.ok()) co_return;
        Vec2 p1 = a.p, p2 = b.p;
        Input d = co_await c.get_point("Specify third point on circle:", p2, [&c, p1, p2](Vec2 p) {
            Vec2 cc; double r;
            return circle_3p(p1, p2, p, cc, r) ? Ents{c.styled(make_circle(cc, r))} : Ents{};
        }, false);
        Vec2 cc; double r;
        if (d.ok() && circle_3p(p1, p2, d.p, cc, r)) { c.checkpoint(); c.doc.add(c.styled(make_circle(cc, r))); c.last_radius = r; }
        co_return;
    }
    if (in.kw("2P")) {
        Input a = co_await c.get_point("Specify first end point of circle's diameter:");
        if (!a.ok()) co_return;
        Vec2 p1 = a.p;
        Input b = co_await c.get_point("Specify second end point of circle's diameter:", p1, [&c, p1](Vec2 p) {
            return Ents{c.styled(make_circle((p1 + p) * 0.5, dist(p1, p) / 2))};
        });
        if (b.ok() && !near(b.p, p1)) { c.checkpoint(); c.doc.add(c.styled(make_circle((p1 + b.p) * 0.5, dist(p1, b.p) / 2))); c.last_radius = dist(p1, b.p) / 2; }
        co_return;
    }
    if (!in.ok()) co_return;
    Vec2 ctr = in.p;
    double r = c.last_radius;
    in = co_await c.get_distance("Specify radius of circle or [Diameter] <" + fmt(c.last_radius) + ">:", ctr,
                                 [&c, ctr](Vec2 p) { return Ents{c.styled(make_circle(ctr, dist(ctr, p)))}; });
    if (in.kw("Diameter")) {
        in = co_await c.get_distance("Specify diameter of circle <" + fmt(c.last_radius * 2) + ">:", ctr,
                                     [&c, ctr](Vec2 p) { return Ents{c.styled(make_circle(ctr, dist(ctr, p) / 2))}; });
        if (in.ok()) r = in.v / 2;
        else if (in.st != Input::None) co_return;
    } else if (in.ok()) r = in.v;
    else if (in.st != Input::None) co_return;
    if (r <= 0) { c.print("Value must be positive and nonzero."); co_return; }
    c.last_radius = r;
    c.checkpoint();
    c.doc.add(c.styled(make_circle(ctr, r)));
}

// Center / start / end arc (CCW, Ctrl flips direction)
static Task<> arc_cse(Cad& c, Vec2 ctr, std::optional<Vec2> start_opt) {
    Vec2 start;
    if (start_opt) start = *start_opt;
    else {
        Input s = co_await c.get_point("Specify start point of arc:", ctr);
        if (!s.ok()) co_return;
        start = s.p;
    }
    double r = dist(ctr, start), a0 = angle_of(start - ctr);
    auto build = [&c, ctr, r, a0](double a1) {
        bool cw = ImGui::GetIO().KeyCtrl;
        return c.styled(cw ? make_arc(ctr, r, a1, a0) : make_arc(ctr, r, a0, a1));
    };
    Input e = co_await c.get_point("Specify end point of arc (hold Ctrl to switch direction) or [Angle]:", ctr,
                                   [build, ctr](Vec2 p) { return Ents{build(angle_of(p - ctr))}; });
    if (e.kw("Angle")) {
        Input a = co_await c.get_angle("Specify included angle (hold Ctrl to switch direction):", ctr,
                                       [build, ctr](Vec2 p) { return Ents{build(angle_of(p - ctr))}; });
        if (!a.ok()) co_return;
        c.checkpoint();
        c.doc.add(c.styled(make_arc(ctr, r, a0, a0 + rad(a.v))));
        co_return;
    }
    if (!e.ok()) co_return;
    c.checkpoint();
    c.doc.add(build(angle_of(e.p - ctr)));
}

static Task<> cmd_arc(Cad& c) {
    Input in = co_await c.get_point("Specify start point of arc or [Center]:");
    if (in.kw("Center")) {
        Input ct = co_await c.get_point("Specify center point of arc:");
        if (!ct.ok()) co_return;
        co_await arc_cse(c, ct.p, std::nullopt);
        co_return;
    }
    if (!in.ok()) co_return;
    Vec2 p1 = in.p;
    in = co_await c.get_point("Specify second point of arc or [Center/End]:", p1);
    if (in.kw("Center")) {
        Input ct = co_await c.get_point("Specify center point of arc:", p1);
        if (!ct.ok()) co_return;
        co_await arc_cse(c, ct.p, p1);
        co_return;
    }
    if (in.kw("End")) {
        Input e = co_await c.get_point("Specify end point of arc:", p1);
        if (!e.ok()) co_return;
        Vec2 p3 = e.p;
        Input ct = co_await c.get_point("Specify center point of arc:", p3, [&c, p1, p3](Vec2 cc) {
            return Ents{c.styled(make_arc(cc, dist(cc, p1), angle_of(p1 - cc), angle_of(p3 - cc)))};
        });
        if (!ct.ok()) co_return;
        c.checkpoint();
        c.doc.add(c.styled(make_arc(ct.p, dist(ct.p, p1), angle_of(p1 - ct.p), angle_of(p3 - ct.p))));
        co_return;
    }
    if (!in.ok()) co_return;
    Vec2 p2 = in.p;
    in = co_await c.get_point("Specify end point of arc:", p2, [&c, p1, p2](Vec2 p) {
        Seg s;
        return arc_3p(p1, p2, p, s) ? Ents{c.styled(make_arc_from_seg(s))} : Ents{};
    }, false);
    Seg s;
    if (in.ok() && arc_3p(p1, p2, in.p, s)) { c.checkpoint(); c.doc.add(c.styled(make_arc_from_seg(s))); }
}

static Entity rect_entity(Vec2 a, Vec2 b) {
    return make_polyline({{a, 0}, {{b.x, a.y}, 0}, {b, 0}, {{a.x, b.y}, 0}}, true);
}

static Task<> cmd_rectang(Cad& c) {
    static double last_len = 10, last_wid = 10;
    Input in = co_await c.get_point("Specify first corner point:");
    if (!in.ok()) co_return;
    Vec2 p1 = in.p;
    bool fixed = false;
    for (;;) {
        auto pv = [&c, p1, &fixed](Vec2 p) {
            Vec2 q = p;
            if (fixed) q = p1 + Vec2{(p.x >= p1.x ? 1 : -1) * last_len, (p.y >= p1.y ? 1 : -1) * last_wid};
            return Ents{c.styled(rect_entity(p1, q))};
        };
        in = co_await c.get_point("Specify other corner point or [Dimensions]:", p1, pv, false);
        if (in.kw("Dimensions")) {
            Input l = co_await c.get_distance("Specify length for rectangles <" + fmt(last_len) + ">:", p1);
            if (l.ok()) last_len = l.v;
            Input w = co_await c.get_distance("Specify width for rectangles <" + fmt(last_wid) + ">:", p1);
            if (w.ok()) last_wid = w.v;
            fixed = true;
            continue;
        }
        if (!in.ok()) co_return;
        Vec2 q = in.p;
        if (fixed) q = p1 + Vec2{(q.x >= p1.x ? 1 : -1) * last_len, (q.y >= p1.y ? 1 : -1) * last_wid};
        if (std::fabs(q.x - p1.x) < EPS || std::fabs(q.y - p1.y) < EPS) { c.print("Rectangle has zero area."); co_return; }
        c.checkpoint();
        c.doc.add(c.styled(rect_entity(p1, q)));
        co_return;
    }
}

static Entity polygon_entity(Vec2 ctr, double R, double a0, int n) {
    std::vector<Vtx> v;
    for (int i = 0; i < n; i++) v.push_back({ctr + dir(a0 + TAU * i / n) * R, 0});
    return make_polyline(v, true);
}

static Task<> cmd_polygon(Cad& c) {
    Input in = co_await c.get_integer("Enter number of sides <" + std::to_string(c.polygon_sides) + ">:");
    if (in.ok()) {
        if (in.v < 3 || in.v > 1024) { c.print("Requires an integer between 3 and 1024."); co_return; }
        c.polygon_sides = (int)in.v;
    } else if (in.st != Input::None) co_return;
    int n = c.polygon_sides;
    in = co_await c.get_point("Specify center of polygon or [Edge]:");
    if (in.kw("Edge")) {
        Input a = co_await c.get_point("Specify first endpoint of edge:");
        if (!a.ok()) co_return;
        Vec2 e1 = a.p;
        auto build = [n](Vec2 p, Vec2 q) {
            std::vector<Vtx> v;
            Vec2 cur = p, d = q - p;
            for (int i = 0; i < n; i++) { v.push_back({cur, 0}); cur = cur + d; d = rotate(d, TAU / n); }
            return make_polyline(v, true);
        };
        Input b = co_await c.get_point("Specify second endpoint of edge:", e1, [&c, e1, build](Vec2 p) { return Ents{c.styled(build(e1, p))}; });
        if (!b.ok() || near(b.p, e1)) co_return;
        c.checkpoint();
        c.doc.add(c.styled(build(e1, b.p)));
        co_return;
    }
    if (!in.ok()) co_return;
    Vec2 ctr = in.p;
    static bool inscribed = true;
    in = co_await c.get_keyword(std::string("Enter an option [Inscribed in circle/Circumscribed about circle] <") + (inscribed ? "I" : "C") + ">:");
    if (in.kw("Inscribed in circle")) inscribed = true;
    else if (in.kw("Circumscribed about circle")) inscribed = false;
    else if (in.st != Input::None) co_return;
    bool ins = inscribed;
    auto build = [n, ins, ctr](Vec2 p) {
        double d = dist(ctr, p), a = angle_of(p - ctr);
        if (ins) return polygon_entity(ctr, d, a, n);
        return polygon_entity(ctr, d / std::cos(PI / n), a - PI / n, n);
    };
    in = co_await c.get_distance("Specify radius of circle:", ctr, [&c, build](Vec2 p) { return Ents{c.styled(build(p))}; });
    if (!in.ok() || in.v <= 0) co_return;
    c.checkpoint();
    if (in.picked) c.doc.add(c.styled(build(in.p)));
    else { // typed radius: bottom edge horizontal
        double a0 = -PI / 2 - PI / n;
        double R = ins ? in.v : in.v / std::cos(PI / n);
        c.doc.add(c.styled(polygon_entity(ctr, R, a0, n)));
    }
}

static Task<> cmd_ellipse(Cad& c) {
    Input in = co_await c.get_point("Specify axis endpoint of ellipse or [Center]:");
    Vec2 ctr, major;
    if (in.kw("Center")) {
        Input a = co_await c.get_point("Specify center of ellipse:");
        if (!a.ok()) co_return;
        ctr = a.p;
        Input b = co_await c.get_point("Specify endpoint of axis:", ctr);
        if (!b.ok() || near(b.p, ctr)) co_return;
        major = b.p - ctr;
    } else {
        if (!in.ok()) co_return;
        Vec2 a1 = in.p;
        Input b = co_await c.get_point("Specify other endpoint of axis:", a1);
        if (!b.ok() || near(b.p, a1)) co_return;
        ctr = (a1 + b.p) * 0.5;
        major = b.p - ctr;
    }
    auto ratio_at = [ctr, major](Vec2 p) {
        double h = std::fabs(cross(norm(major), p - ctr));
        return std::max(h / len(major), 1e-6);
    };
    in = co_await c.get_distance("Specify distance to other axis or [Rotation]:", ctr,
                                 [&c, ctr, major, ratio_at](Vec2 p) { return Ents{c.styled(make_ellipse(ctr, major, ratio_at(p)))}; });
    double ratio;
    if (in.kw("Rotation")) {
        Input a = co_await c.get_angle("Specify rotation around major axis:", ctr);
        if (!a.ok()) co_return;
        ratio = std::fabs(std::cos(rad(a.v)));
        if (ratio < 1e-6 || ratio > 1) { c.print("Invalid rotation."); co_return; }
    } else if (in.ok()) {
        ratio = in.picked ? ratio_at(in.p) : in.v / len(major);
    } else co_return;
    if (ratio <= 0) co_return;
    c.checkpoint();
    c.doc.add(c.styled(make_ellipse(ctr, major, ratio)));
}

static const char* pattern_name(HatchPat p) {
    switch (p) { case HatchPat::Solid: return "SOLID"; case HatchPat::ANSI31: return "ANSI31"; case HatchPat::ANSI37: return "ANSI37"; }
    return "";
}

static Entity hatch_proto(const Cad& c) {
    Entity h;
    h.type = EType::Hatch;
    h.pat = c.hatch_pat;
    h.pscale = c.hatch_scale;
    h.pangle = rad(c.hatch_angle);
    h.color = c.cur_color;
    return h;
}

static Task<> cmd_hatch(Cad& c) {
    for (;;) {
        Req r;
        r.kind = ReqKind::Point;
        r.prompt = std::string("Pick internal point or [Select objects/Pattern/sCale/Angle]:");
        r.snap = false;
        r.rubber = false;
        Input in = co_await c.ask(std::move(r));
        if (in.st == Input::None) break;
        if (in.kw("Pattern")) {
            Input k = co_await c.get_keyword(std::string("Enter a pattern name or [Solid/ANSI31/ANSI37] <") + pattern_name(c.hatch_pat) + ">:");
            if (k.kw("Solid")) c.hatch_pat = HatchPat::Solid;
            else if (k.kw("ANSI31")) c.hatch_pat = HatchPat::ANSI31;
            else if (k.kw("ANSI37")) c.hatch_pat = HatchPat::ANSI37;
            continue;
        }
        if (in.kw("sCale")) {
            Input k = co_await c.get_real("Specify a scale for the pattern <" + fmt(c.hatch_scale) + ">:");
            if (k.ok() && k.v > 0) c.hatch_scale = k.v;
            continue;
        }
        if (in.kw("Angle")) {
            Input k = co_await c.get_real("Specify an angle for the pattern <" + fmt(c.hatch_angle) + ">:");
            if (k.ok()) c.hatch_angle = k.v;
            continue;
        }
        if (in.kw("Select objects")) {
            auto ids = co_await c.select_objects();
            std::vector<uint32_t> closed;
            for (auto id : ids) if (auto* e = c.doc.find(id); e && is_closed_curve(*e)) closed.push_back(id);
            if (closed.empty()) { c.print("No valid hatch boundary selected (closed polylines, circles or ellipses)."); continue; }
            Entity h = hatch_proto(c);
            h.bnd = closed;
            loops_from_entities(c.doc, closed, h.loops);
            c.checkpoint();
            c.doc.add(h);
            continue;
        }
        if (!in.ok()) break;
        Entity h = hatch_proto(c);
        if (!find_boundary(c.doc, in.p, h.loops)) { c.print("Valid hatch boundary not found."); continue; }
        h.has_seed = true;
        h.seed = in.p;
        c.checkpoint();
        c.doc.add(h);
    }
}

static Task<> cmd_text(Cad& c) {
    c.print("Current text style:  \"Standard\"  Text height:  " + fmt(c.text_height) + "  Annotative:  No  Justify:  Left");
    Input in = co_await c.get_point("Specify start point of text:");
    if (!in.ok()) co_return;
    Vec2 p = in.p;
    in = co_await c.get_distance("Specify height <" + fmt(c.text_height) + ">:", p);
    if (in.ok() && in.v > 0) c.text_height = in.v;
    else if (in.st != Input::None) co_return;
    double h = c.text_height;
    in = co_await c.get_angle("Specify rotation angle of text <0>:", p);
    double rot = in.ok() ? rad(in.v) : 0.0;
    for (;;) {
        Vec2 ins = p;
        in = co_await c.get_string("Enter text:", [&c, ins, h, rot](Vec2) {
            return Ents{c.styled(make_text(ins, h, rot, c.input + "_"))};
        });
        if (!in.ok()) break;
        c.checkpoint();
        c.doc.add(c.styled(make_text(p, h, rot, in.s)));
        p = p - perp(dir(rot)) * (h * 1.6667);
    }
}

static Task<> cmd_mtext(Cad& c) {
    c.print("Current text style:  \"Standard\"  Text height:  " + fmt(c.text_height) + "  Annotative:  No");
    Input in = co_await c.get_point("Specify first corner:");
    if (!in.ok()) co_return;
    Vec2 p1 = in.p, p2;
    for (;;) {
        in = co_await c.get_point("Specify opposite corner or [Height/Width]:", p1, [p1](Vec2 p) {
            Entity r = rect_entity(p1, p);
            r.color = 8;
            return Ents{r};
        }, false);
        if (in.kw("Height")) {
            Input h = co_await c.get_distance("Specify height <" + fmt(c.text_height) + ">:", p1);
            if (h.ok() && h.v > 0) c.text_height = h.v;
            continue;
        }
        if (in.kw("Width")) {
            Input w = co_await c.get_distance("Specify width:", p1);
            if (!w.ok()) co_return;
            p2 = p1 + Vec2{w.v, -c.text_height * 3};
            break;
        }
        if (!in.ok()) co_return;
        p2 = in.p;
        break;
    }
    Vec2 tl{std::min(p1.x, p2.x), std::max(p1.y, p2.y)};
    double w = std::fabs(p2.x - p1.x);
    c.mtext_buf.clear();
    c.mtext_height = c.text_height;
    Req r;
    r.kind = ReqKind::MText;
    r.prompt = "Enter text in the MText editor, then click OK:";
    in = co_await c.ask(std::move(r));
    if (!in.ok() || in.s.empty()) co_return;
    c.text_height = c.mtext_height;
    c.checkpoint();
    c.doc.add(c.styled(make_mtext(tl, w, c.mtext_height, in.s)));
}

// ---------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------
static Task<> cmd_erase(Cad& c) {
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    c.checkpoint();
    for (auto id : ids) c.doc.erase(id);
}

static Task<> cmd_move(Cad& c) {
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    Input in = co_await c.get_point("Specify base point or [Displacement] <Displacement>:");
    Vec2 disp;
    if (in.kw("Displacement") || in.st == Input::None) {
        Input d = co_await c.get_point("Specify displacement <0.0000, 0.0000>:");
        if (!d.ok()) co_return;
        disp = d.p;
    } else if (in.ok()) {
        Vec2 base = in.p;
        Input d = co_await c.get_point("Specify second point or <use first point as displacement>:", base,
                                       [&c, &ids, base](Vec2 p) { return ghosts(c, ids, Xform::translate(p - base)); });
        if (d.ok()) disp = d.p - base;
        else if (d.st == Input::None) disp = base;
        else co_return;
    } else co_return;
    c.checkpoint();
    apply_xform(c, ids, Xform::translate(disp));
}

static Task<> cmd_copy(Cad& c) {
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    c.print("Current settings:  Copy mode = Multiple");
    Input in = co_await c.get_point("Specify base point or [Displacement] <Displacement>:");
    if (!in.ok()) co_return;
    Vec2 base = in.p;
    std::vector<std::vector<uint32_t>> made;
    for (;;) {
        std::string pr = made.empty() ? "Specify second point or [Array] <use first point as displacement>:" : "Specify second point or [Exit/Undo] <Exit>:";
        in = co_await c.get_point(pr, base, [&c, &ids, base](Vec2 p) { return ghosts(c, ids, Xform::translate(p - base)); });
        if (in.kw("Undo")) {
            if (!made.empty()) { for (auto id : made.back()) c.doc.erase(id); made.pop_back(); }
            continue;
        }
        if (in.kw("Array")) { c.print("Array is not supported; specify a second point."); continue; }
        if (in.st == Input::None || in.kw("Exit")) {
            if (made.empty() && in.st == Input::None) { c.checkpoint(); copy_entities(c, ids, Xform::translate(base)); }
            break;
        }
        if (!in.ok()) break;
        c.checkpoint();
        made.push_back(copy_entities(c, ids, Xform::translate(in.p - base)));
    }
}

static Task<> cmd_rotate(Cad& c) {
    c.print("Current positive angle in UCS:  ANGDIR=counterclockwise  ANGBASE=0");
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    Input in = co_await c.get_point("Specify base point:");
    if (!in.ok()) co_return;
    Vec2 base = in.p;
    bool copy = false;
    double ang = 0;
    for (;;) {
        in = co_await c.get_angle("Specify rotation angle or [Copy/Reference] <0>:", base,
                                  [&c, &ids, base](Vec2 p) { return ghosts(c, ids, Xform::rotate_about(base, angle_of(p - base))); });
        if (in.kw("Copy")) { copy = true; c.print("Rotating a copy of the selected objects."); continue; }
        if (in.kw("Reference")) {
            Input r = co_await c.get_angle("Specify the reference angle <0>:", base);
            double ref = r.ok() ? r.v : 0;
            Input n = co_await c.get_angle("Specify the new angle:", base,
                                           [&c, &ids, base, ref](Vec2 p) { return ghosts(c, ids, Xform::rotate_about(base, angle_of(p - base) - rad(ref))); });
            if (!n.ok()) co_return;
            ang = rad(n.v - ref);
            break;
        }
        if (in.ok()) { ang = rad(in.v); break; }
        if (in.st == Input::None) break;
        co_return;
    }
    c.checkpoint();
    Xform x = Xform::rotate_about(base, ang);
    if (copy) copy_entities(c, ids, x);
    else apply_xform(c, ids, x);
}

static Task<> cmd_scale(Cad& c) {
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    Input in = co_await c.get_point("Specify base point:");
    if (!in.ok()) co_return;
    Vec2 base = in.p;
    bool copy = false;
    double f = 1;
    for (;;) {
        in = co_await c.get_distance("Specify scale factor or [Copy/Reference]:", base,
                                     [&c, &ids, base](Vec2 p) { double s = dist(base, p); return s > EPS ? ghosts(c, ids, Xform::scale_about(base, s)) : Ents{}; });
        if (in.kw("Copy")) { copy = true; c.print("Scaling a copy of the selected objects."); continue; }
        if (in.kw("Reference")) {
            Input r = co_await c.get_distance("Specify reference length <1.0000>:", base);
            double ref = r.ok() ? r.v : 1.0;
            if (ref <= 0) co_return;
            Input n = co_await c.get_distance("Specify new length:", base,
                                              [&c, &ids, base, ref](Vec2 p) { return ghosts(c, ids, Xform::scale_about(base, dist(base, p) / ref)); });
            if (!n.ok()) co_return;
            f = n.v / ref;
            break;
        }
        if (in.ok()) { f = in.v; break; }
        co_return;
    }
    if (f <= 0) { c.print("Value must be positive and nonzero."); co_return; }
    c.checkpoint();
    Xform x = Xform::scale_about(base, f);
    if (copy) copy_entities(c, ids, x);
    else apply_xform(c, ids, x);
}

static Task<> cmd_mirror(Cad& c) {
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    Input a = co_await c.get_point("Specify first point of mirror line:");
    if (!a.ok()) co_return;
    Vec2 p1 = a.p;
    Input b = co_await c.get_point("Specify second point of mirror line:", p1,
                                   [&c, &ids, p1](Vec2 p) { return near(p, p1) ? Ents{} : ghosts(c, ids, Xform::mirror(p1, p)); });
    if (!b.ok() || near(b.p, p1)) co_return;
    Input k = co_await c.get_keyword("Erase source objects? [Yes/No] <No>:");
    c.checkpoint();
    Xform x = Xform::mirror(p1, b.p);
    if (k.kw("Yes")) apply_xform(c, ids, x);
    else copy_entities(c, ids, x);
}

static Task<> trim_extend(Cad& c, bool extend_mode) {
    c.print("Current settings: Projection=UCS, Edge=None, Mode=Quick");
    std::vector<Snapshot> local;
    for (;;) {
        Input in = co_await c.get_pick(extend_mode ? "Select object to extend or shift-select to trim or [eRase/Undo]:"
                                                   : "Select object to trim or shift-select to extend or [eRase/Undo]:");
        if (in.st == Input::None) break;
        if (in.kw("Undo")) {
            if (local.empty()) c.print("All operations have been undone.");
            else { c.restore(local.back()); local.pop_back(); }
            continue;
        }
        if (in.kw("eRase")) {
            Input e = co_await c.get_pick("Select objects to erase:");
            if (e.ok()) { local.push_back(c.snapshot()); c.checkpoint(); c.doc.erase(e.ent); }
            continue;
        }
        if (!in.ok()) break;
        Snapshot before = c.snapshot();
        c.checkpoint();
        bool do_extend = extend_mode != in.shift;
        bool changed;
        if (do_extend) {
            changed = extend_entity(c.doc, in.ent, in.p);
            if (!changed) c.print("Object does not intersect an edge.");
        } else {
            changed = trim_entity(c.doc, in.ent, in.p) != TrimResult::Nothing;
            if (!changed) c.print("Cannot TRIM this object.");
        }
        if (changed) { local.push_back(std::move(before)); update_associative(c.doc); }
    }
}
static Task<> cmd_trim(Cad& c) { co_await trim_extend(c, false); }
static Task<> cmd_extend(Cad& c) { co_await trim_extend(c, true); }

static Task<> cmd_join(Cad& c) {
    auto ids = co_await c.select_objects("Select source object or multiple objects to join at once:");
    if (ids.size() < 2) { c.print("Select at least 2 objects to join."); co_return; }
    c.checkpoint();
    std::vector<uint32_t> created;
    int n = join_entities(c.doc, ids, created);
    if (n == 0) c.print("0 objects joined. Objects must be lines, arcs or open polylines touching end to end.");
    else c.print(std::to_string(n) + " objects converted to " + std::to_string(created.size()) + (created.size() == 1 ? " object" : " objects"));
}

static Task<> cmd_explode(Cad& c) {
    auto ids = co_await c.select_objects();
    if (ids.empty()) co_return;
    c.checkpoint();
    int n = explode_entities(c.doc, ids);
    if (n < (int)ids.size()) c.print(std::to_string(ids.size() - n) + " object(s) could not be exploded.");
}

static Task<> cmd_group(Cad& c) {
    auto ids = co_await c.select_objects("Select objects or [Name/Description]:");
    if (ids.empty()) co_return;
    c.checkpoint();
    uint32_t g = c.doc.next_group++;
    for (auto id : ids) if (auto* e = c.doc.find(id)) e->group = g;
    c.doc.touch();
    c.print("Group \"*A" + std::to_string(g) + "\" has been created.");
}

static Task<> cmd_ungroup(Cad& c) {
    bool prev = c.group_sel;
    c.group_sel = true;
    auto ids = co_await c.select_objects("Select group or [Name]:");
    c.group_sel = prev;
    std::set<uint32_t> groups;
    for (auto id : ids) if (auto* e = c.doc.find(id); e && e->group) groups.insert(e->group);
    if (groups.empty()) { c.print("Selected objects are not in a group."); co_return; }
    c.checkpoint();
    for (auto& e : c.doc.ents) if (groups.count(e.group)) e.group = 0;
    c.doc.touch();
    for (auto g : groups) c.print("Group \"*A" + std::to_string(g) + "\" has been exploded.");
}

// ---------------------------------------------------------------------------
// Annotation
// ---------------------------------------------------------------------------
static LinDir auto_dir(Vec2 p1, Vec2 p2, Vec2 cur) {
    double mnx = std::min(p1.x, p2.x), mxx = std::max(p1.x, p2.x), mny = std::min(p1.y, p2.y), mxy = std::max(p1.y, p2.y);
    bool inx = cur.x >= mnx && cur.x <= mxx, iny = cur.y >= mny && cur.y <= mxy;
    if (inx && !iny) return LinDir::Horizontal;
    if (iny && !inx) return LinDir::Vertical;
    double dx = std::max(mnx - cur.x, cur.x - mxx), dy = std::max(mny - cur.y, cur.y - mxy);
    return dy >= dx ? LinDir::Horizontal : LinDir::Vertical;
}

static Task<> dim_linear(Cad& c, bool aligned) {
    Entity d;
    d.type = EType::Dim;
    d.dt = aligned ? DimType::Aligned : DimType::Linear;
    d.color = c.cur_color;
    Input in = co_await c.get_point("Specify first extension line origin or <select object>:");
    if (in.st == Input::None) {
        Input pk = co_await c.get_pick("Select object to dimension:");
        if (!pk.ok()) co_return;
        const Entity* e = c.doc.find(pk.ent);
        if (!e || (e->type != EType::Line && e->type != EType::Polyline && e->type != EType::Arc)) { c.print("Object selected is not a line, polyline segment or arc."); co_return; }
        if (e->type == EType::Polyline) {
            auto segs = entity_segs(*e);
            int best = 0; double bd = 1e300;
            for (int i = 0; i < (int)segs.size(); i++) { double dd = seg_dist(segs[i], pk.p); if (dd < bd) { bd = dd; best = i; } }
            d.r1 = {e->id, {KeyKind::Vertex, best}};
            d.r2 = {e->id, {KeyKind::Vertex, (best + 1) % (int)e->v.size()}};
        } else {
            d.r1 = {e->id, {KeyKind::Vertex, 0}};
            d.r2 = {e->id, {KeyKind::Vertex, 1}};
        }
        key_point(c.doc, *e, d.r1.key, d.p1);
        key_point(c.doc, *e, d.r2.key, d.p2);
    } else {
        if (!in.ok()) co_return;
        d.p1 = in.p;
        d.r1 = in.snap.ref();
        in = co_await c.get_point("Specify second extension line origin:", d.p1);
        if (!in.ok()) co_return;
        d.p2 = in.p;
        d.r2 = in.snap.ref();
    }
    if (near(d.p1, d.p2)) { c.print("Extension line origins coincide."); co_return; }
    bool forced = false;
    for (;;) {
        auto place = [aligned, &forced](Entity e, Vec2 cur) {
            if (!aligned && !forced) e.ldir = auto_dir(e.p1, e.p2, cur);
            e.off = dot(cur - e.p1, perp(dim_direction(e, e.p1, e.p2)));
            return e;
        };
        auto pv = [&d, place](Vec2 p) { Entity g = place(d, p); g.r1 = {}; g.r2 = {}; return Ents{g}; };
        in = co_await c.get_point(aligned ? "Specify dimension line location or [Text]:" : "Specify dimension line location or [Text/Horizontal/Vertical]:", {}, pv, false);
        if (in.kw("Text")) {
            Input t = co_await c.get_string("Enter dimension text <" + format_number(dim_value(c.doc, d), c.doc.dimstyle.prec) + ">:");
            if (t.ok()) d.override_text = t.s;
            continue;
        }
        if (in.kw("Horizontal")) { d.ldir = LinDir::Horizontal; forced = true; continue; }
        if (in.kw("Vertical")) { d.ldir = LinDir::Vertical; forced = true; continue; }
        if (!in.ok()) co_return;
        d = place(d, in.p);
        break;
    }
    c.checkpoint();
    c.doc.add(d);
    c.print("Dimension text = " + format_number(dim_value(c.doc, d), c.doc.dimstyle.prec));
}
static Task<> cmd_dimlinear(Cad& c) { co_await dim_linear(c, false); }
static Task<> cmd_dimaligned(Cad& c) { co_await dim_linear(c, true); }

static Task<> dim_radial(Cad& c, bool diameter) {
    const Entity* e = nullptr;
    for (;;) {
        Input pk = co_await c.get_pick("Select arc or circle:");
        if (!pk.ok()) co_return;
        e = c.doc.find(pk.ent);
        if (e && (e->type == EType::Circle || e->type == EType::Arc)) break;
        c.print("Object selected is not a circle or arc.");
    }
    Entity d;
    d.type = EType::Dim;
    d.dt = diameter ? DimType::Diameter : DimType::Radius;
    d.rent = e->id;
    d.c = e->c;
    d.r = e->r;
    d.color = c.cur_color;
    c.print("Dimension text = " + format_number(diameter ? 2 * e->r : e->r, c.doc.dimstyle.prec));
    for (;;) {
        auto pv = [&d](Vec2 p) { Entity g = d; g.rent = 0; g.loc = p - g.c; if (len(g.loc) < EPS) g.loc = {g.r, 0}; return Ents{g}; };
        Input in = co_await c.get_point("Specify dimension line location or [Text]:", {}, pv, false);
        if (in.kw("Text")) {
            Input t = co_await c.get_string("Enter dimension text <" + format_number(diameter ? 2 * d.r : d.r, c.doc.dimstyle.prec) + ">:");
            if (t.ok()) d.override_text = t.s;
            continue;
        }
        if (!in.ok()) co_return;
        d.loc = in.p - d.c;
        if (len(d.loc) < EPS) d.loc = {d.r, 0};
        break;
    }
    c.checkpoint();
    c.doc.add(d);
}
static Task<> cmd_dimradius(Cad& c) { co_await dim_radial(c, false); }
static Task<> cmd_dimdiameter(Cad& c) { co_await dim_radial(c, true); }

// ---------------------------------------------------------------------------
// View / utility
// ---------------------------------------------------------------------------
static Task<> cmd_zoom(Cad& c) {
    c.view_user_set = true;
    c.sel = c.pre_sel;
    c.pre_sel.clear();
    Input in = co_await c.get_point("Specify corner of window or [All/Extents/Window] <real time>:");
    if (in.kw("All") || in.kw("Extents")) { c.zoom_extents(); co_return; }
    Vec2 p1;
    if (in.kw("Window")) {
        Input a = co_await c.get_point("Specify first corner:");
        if (!a.ok()) co_return;
        p1 = a.p;
    } else if (in.ok()) p1 = in.p;
    else co_return;
    Input b = co_await c.get_point("Specify opposite corner:", p1, [p1](Vec2 p) { Entity r = rect_entity(p1, p); r.color = 8; return Ents{r}; }, false);
    if (b.ok()) c.zoom_window(p1, b.p);
}

static Task<> cmd_undo(Cad& c) { c.undo(); co_return; }
static Task<> cmd_redo(Cad& c) { c.redo(); co_return; }
static Task<> cmd_properties(Cad& c) { c.sel = c.pre_sel; c.pre_sel.clear(); c.show_properties = true; co_return; }
static Task<> cmd_osnap(Cad& c) { c.sel = c.pre_sel; c.pre_sel.clear(); c.show_osnap_settings = true; co_return; }

static Task<> cmd_grid(Cad& c) {
    Input in = co_await c.get_distance("Specify grid spacing(X) or [ON/OFF] <" + fmt(c.grid_step) + ">:");
    if (in.kw("ON")) c.grid_on = true;
    else if (in.kw("OFF")) c.grid_on = false;
    else if (in.ok() && in.v > 0) { c.grid_step = in.v; c.grid_on = true; }
}

static Task<> cmd_snap(Cad& c) {
    Input in = co_await c.get_distance("Specify snap spacing or [ON/OFF] <" + fmt(c.snap_step) + ">:");
    if (in.kw("ON")) c.snap_on = true;
    else if (in.kw("OFF")) c.snap_on = false;
    else if (in.ok() && in.v > 0) { c.snap_step = in.v; c.snap_on = true; }
}

static Task<> cmd_script(Cad& c) {
    Input in = co_await c.get_string("Enter script file name:");
    if (!in.ok()) co_return;
    std::string path = in.s;
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
    if (!c.load_script(path)) c.print("Cannot find script file \"" + path + "\".");
}

// ---------------------------------------------------------------------------
// Grip editing (STRETCH / MOVE / ROTATE / SCALE / MIRROR, cycled with Enter)
// ---------------------------------------------------------------------------
Task<> grip_edit(Cad& c, std::vector<std::pair<uint32_t, PKey>> gl, Vec2 base) {
    static const char* modes[] = {"STRETCH", "MOVE", "ROTATE", "SCALE", "MIRROR"};
    int mode = 0;
    bool copy = false;
    std::vector<uint32_t> ids = c.sel;
    for (;;) {
        std::string hdr = std::string("** ") + modes[mode] + (copy ? " (multiple)" : "") + " **";
        c.print(hdr);
        auto xf = [&mode, &base](Vec2 p) {
            switch (mode) {
            case 1: return Xform::translate(p - base);
            case 2: return Xform::rotate_about(base, angle_of(p - base));
            case 3: return Xform::scale_about(base, std::max(dist(base, p), 1e-9));
            case 4: return near(p, base) ? Xform{} : Xform::mirror(base, p);
            default: return Xform{};
            }
        };
        PreviewFn pv;
        if (mode == 0) {
            pv = [&c, &gl](Vec2 p) {
                std::map<uint32_t, Entity> tmp;
                for (auto& [id, key] : gl) {
                    if (!tmp.count(id)) {
                        const Entity* e = c.doc.find(id);
                        if (!e) continue;
                        Entity g = *e;
                        if (g.type == EType::Dim) { dim_points(c.doc, *e, g.p1, g.p2); dim_circle(c.doc, *e, g.c, g.r); g.r1 = {}; g.r2 = {}; g.rent = 0; }
                        tmp[id] = g;
                    }
                    move_key(c.doc, tmp[id], key, p);
                }
                Ents out;
                for (auto& [id, e] : tmp) out.push_back(e);
                return out;
            };
        } else {
            pv = [&c, &ids, xf](Vec2 p) { return ghosts(c, ids, xf(p)); };
        }
        Input in;
        std::string pr = std::string("Specify ") + (mode == 0 ? "stretch point" : mode == 1 ? "move point" : mode == 2 ? "rotation angle" : mode == 3 ? "scale factor" : "second point") + " or [Base point/Copy/Undo/eXit]:";
        if (mode == 2) in = co_await c.get_angle(pr, base, pv);
        else if (mode == 3) in = co_await c.get_distance(pr, base, pv);
        else in = co_await c.get_point(pr, base, pv);
        if (in.st == Input::None) { mode = (mode + 1) % 5; continue; }
        if (in.kw("eXit") || in.kw("Undo")) break;
        if (in.kw("Copy")) { copy = true; continue; }
        if (in.kw("Base point")) {
            Input b = co_await c.get_point("Specify base point:");
            if (b.ok()) base = b.p;
            continue;
        }
        if (!in.ok()) break;
        c.checkpoint();
        if (mode == 0) {
            Vec2 target = in.p;
            if (copy) {
                auto made = copy_entities(c, ids, Xform{});
                (void)made;
            }
            for (auto& [id, key] : gl) if (Entity* e = c.doc.find(id)) move_key(c.doc, *e, key, target);
            c.doc.touch();
            update_associative(c.doc);
        } else {
            Xform x;
            if (mode == 2 && !in.picked) x = Xform::rotate_about(base, rad(in.v));
            else if (mode == 3 && !in.picked) x = Xform::scale_about(base, std::max(in.v, 1e-9));
            else x = xf(in.p);
            if (copy) copy_entities(c, ids, x);
            else apply_xform(c, ids, x);
        }
        if (!copy) break;
    }
    c.sel = ids;
}

// ---------------------------------------------------------------------------
const std::vector<CmdDef>& command_table() {
    static const std::vector<CmdDef> t = {
        {"LINE", {"L"}, cmd_line, "Creates straight line segments"},
        {"PLINE", {"PL"}, cmd_pline, "Creates a 2D polyline"},
        {"CIRCLE", {"C"}, cmd_circle, "Creates a circle"},
        {"ARC", {"A"}, cmd_arc, "Creates an arc"},
        {"RECTANG", {"REC", "RECTANGLE"}, cmd_rectang, "Creates a rectangular polyline"},
        {"POLYGON", {"POL"}, cmd_polygon, "Creates an equilateral closed polyline"},
        {"ELLIPSE", {"EL"}, cmd_ellipse, "Creates an ellipse"},
        {"HATCH", {"H", "BH", "BHATCH"}, cmd_hatch, "Fills an enclosed area with a hatch pattern"},
        {"TEXT", {"DT", "DTEXT"}, cmd_text, "Creates single-line text"},
        {"MTEXT", {"T", "MT"}, cmd_mtext, "Creates multiline text"},
        {"ERASE", {"E"}, cmd_erase, "Removes objects from a drawing"},
        {"MOVE", {"M"}, cmd_move, "Moves objects"},
        {"COPY", {"CO", "CP"}, cmd_copy, "Copies objects"},
        {"ROTATE", {"RO"}, cmd_rotate, "Rotates objects around a base point"},
        {"SCALE", {"SC"}, cmd_scale, "Enlarges or reduces objects"},
        {"MIRROR", {"MI"}, cmd_mirror, "Creates a mirrored copy of objects"},
        {"TRIM", {"TR"}, cmd_trim, "Trims objects to meet the edges of other objects"},
        {"EXTEND", {"EX"}, cmd_extend, "Extends objects to meet the edges of other objects"},
        {"JOIN", {"J"}, cmd_join, "Joins objects to form a single object"},
        {"EXPLODE", {"X"}, cmd_explode, "Breaks a polyline into its component objects"},
        {"GROUP", {"G"}, cmd_group, "Creates a group of objects"},
        {"UNGROUP", {"UNG"}, cmd_ungroup, "Disassociates the objects in a group"},
        {"DIMLINEAR", {"DLI"}, cmd_dimlinear, "Creates a linear dimension"},
        {"DIMALIGNED", {"DAL"}, cmd_dimaligned, "Creates an aligned dimension"},
        {"DIMRADIUS", {"DRA"}, cmd_dimradius, "Creates a radius dimension"},
        {"DIMDIAMETER", {"DDI"}, cmd_dimdiameter, "Creates a diameter dimension"},
        {"ZOOM", {"Z"}, cmd_zoom, "Zooms the view"},
        {"U", {"UNDO"}, cmd_undo, "Reverses the most recent operation"},
        {"REDO", {}, cmd_redo, "Reverses the effects of the previous UNDO"},
        {"PROPERTIES", {"PR", "CH", "MO", "PROPS"}, cmd_properties, "Shows the Properties palette"},
        {"OSNAP", {"OS", "DSETTINGS", "DS", "SE"}, cmd_osnap, "Sets object snap modes"},
        {"GRID", {}, cmd_grid, "Displays a grid pattern"},
        {"SNAP", {"SN"}, cmd_snap, "Restricts cursor movement to intervals"},
        {"SCRIPT", {"SCR"}, cmd_script, "Executes a sequence of commands from a script file"},
    };
    return t;
}

} // namespace cad
