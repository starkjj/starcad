#include "cad.h"

#include <imgui_internal.h>

#include <cstring>

namespace cad {

extern ImFont* g_text_font;
void set_text_font(ImFont* f) { g_text_font = f; }

// Default palette
static const ImU32 COL_BG = IM_COL32(33, 40, 48, 255);
static const ImU32 COL_GRID_MINOR = IM_COL32(40, 48, 58, 255);
static const ImU32 COL_GRID_MAJOR = IM_COL32(51, 61, 74, 255);
static const ImU32 COL_SEL = IM_COL32(90, 150, 255, 255);
static const ImU32 COL_GRIP = IM_COL32(20, 90, 255, 255);
static const ImU32 COL_GRIP_HOVER = IM_COL32(255, 110, 120, 255);
static const ImU32 COL_SNAP = IM_COL32(150, 255, 60, 255);

ImU32 aci_color(int aci, float alpha) {
    static const ImVec4 tbl[] = {
        {1, 1, 1, 1},         // 0 ByBlock -> white
        {1, 0, 0, 1},         // 1 red
        {1, 1, 0, 1},         // 2 yellow
        {0, 1, 0, 1},         // 3 green
        {0, 1, 1, 1},         // 4 cyan
        {0.15f, 0.35f, 1, 1}, // 5 blue (lifted slightly for the dark background)
        {1, 0, 1, 1},         // 6 magenta
        {1, 1, 1, 1},         // 7 white
        {0.5f, 0.5f, 0.5f, 1},// 8 dark gray
        {0.75f, 0.75f, 0.75f, 1}, // 9 light gray
    };
    ImVec4 c = tbl[(aci >= 0 && aci <= 9) ? aci : 7];
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

static ImVec2 lerp2(ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

static void dashed_line(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float dash = 6, float gap = 4) {
    float L = std::hypot(b.x - a.x, b.y - a.y);
    if (L < 1) return;
    for (float s = 0; s < L; s += dash + gap) dl->AddLine(lerp2(a, b, s / L), lerp2(a, b, std::min(s + dash, L) / L), col, th);
}

// ---------------------------------------------------------------------------
// World-space text: glyph quads emitted directly so text scales and rotates with the drawing.
// ---------------------------------------------------------------------------
void draw_world_text(ImDrawList* dl, const Cad& c, const std::string& s, Vec2 ins, double height, double rot, ImU32 col, bool top_anchor, double wrap) {
    if (s.empty()) return;
    ImFont* font = g_text_font ? g_text_font : ImGui::GetFont();
    double px = height / TEXT_CAP_RATIO * c.zoom;
    if (px < 2.0) {
        // Too small to read: draw a faint stub so the text remains visible
        ImVec2 o = c.to_screen(ins);
        Vec2 d = dir(rot);
        double w = std::min<double>(s.size() * px * 0.5, 4000);
        dl->AddLine(o, ImVec2(o.x + (float)(d.x * w), o.y - (float)(d.y * w)), (col & 0x00FFFFFF) | 0x60000000);
        return;
    }
    const float q = (float)std::clamp(std::exp2(std::round(std::log2(px) * 4.0) / 4.0), 8.0, 96.0);
    const double k = px / q;
    ImFontBaked* baked = font->GetFontBaked(q);
    const ImVec2 o = c.to_screen(ins);
    const double cs = std::cos(rot), sn = std::sin(rot);
    auto xf = [&](double lx, double ly) {
        double x = lx * k, y = ly * k;
        return ImVec2((float)(o.x + x * cs + y * sn), (float)(o.y - x * sn + y * cs));
    };
    // Cull text whose anchor is far outside the canvas
    double reach = (s.size() + 2) * px * 1.5 + px * 4;
    if (o.x < c.canvas_min.x - reach || o.x > c.canvas_max.x + reach || o.y < c.canvas_min.y - reach || o.y > c.canvas_max.y + reach) return;

    const float wrap_px = wrap > 0 ? (float)(wrap * c.zoom / k) : 0.0f;
    double y = top_anchor ? 0.0 : -baked->Ascent;
    const char* p = s.c_str();
    const char* end = p + s.size();
    while (p <= end) {
        const char* line_end = (const char*)std::memchr(p, '\n', end - p);
        if (!line_end) line_end = end;
        const char* seg_end = line_end;
        if (wrap_px > 0 && p < line_end) {
            seg_end = font->CalcWordWrapPosition(q, p, line_end, wrap_px);
            if (seg_end <= p) seg_end = p + 1;
        }
        double x = 0;
        for (const char* t = p; t < seg_end;) {
            unsigned int ch = 0;
            int n = ImTextCharFromUtf8(&ch, t, seg_end);
            t += n > 0 ? n : 1;
            if (ch == '\r') continue;
            const ImFontGlyph* g = baked->FindGlyph((ImWchar)ch);
            if (!g) continue;
            if (g->Visible) {
                dl->PrimReserve(6, 4);
                dl->PrimQuadUV(xf(x + g->X0, y + g->Y0), xf(x + g->X1, y + g->Y0), xf(x + g->X1, y + g->Y1), xf(x + g->X0, y + g->Y1),
                               ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0), ImVec2(g->U1, g->V1), ImVec2(g->U0, g->V1), col);
            }
            x += g->AdvanceX;
        }
        y += q;
        p = seg_end;
        if (seg_end < line_end) { while (p < line_end && (*p == ' ' || *p == '\t')) p++; continue; }
        if (line_end >= end) break;
        p = line_end + 1;
    }
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------
static int arc_steps(double r_px, double sweep) {
    double full = std::clamp(r_px * TAU / 4.0, 16.0, 720.0);
    return std::clamp((int)std::ceil(full * std::fabs(sweep) / TAU), 2, 720);
}

static void draw_segs(ImDrawList* dl, const Cad& c, const std::vector<Seg>& segs, bool closed, ImU32 col, float th) {
    static std::vector<ImVec2> pts;
    pts.clear();
    for (size_t i = 0; i < segs.size(); i++) {
        const Seg& s = segs[i];
        if (!s.arc) pts.push_back(c.to_screen(s.a));
        else {
            int n = arc_steps(s.r * c.zoom, s.sw);
            for (int k = 0; k < n; k++) pts.push_back(c.to_screen(s.at((double)k / n)));
        }
    }
    if (!closed && !segs.empty()) pts.push_back(c.to_screen(segs.back().b));
    if (pts.size() >= 2) dl->AddPolyline(pts.data(), (int)pts.size(), col, th, closed ? ImDrawFlags_Closed : 0);
}

static void hatch_lines(ImDrawList* dl, const Cad& c, const std::vector<std::vector<Vec2>>& loops, double ang, double sp, ImU32 col, float th) {
    Vec2 u = dir(ang), n = perp(u);
    double dmin = 1e300, dmax = -1e300;
    for (auto& l : loops) for (auto& p : l) { double d = dot(p, n); dmin = std::min(dmin, d); dmax = std::max(dmax, d); }
    // Clip to the visible area
    Vec2 corners[4] = {c.to_world(c.canvas_min), c.to_world(c.canvas_max), c.to_world(ImVec2(c.canvas_min.x, c.canvas_max.y)), c.to_world(ImVec2(c.canvas_max.x, c.canvas_min.y))};
    double vmin = 1e300, vmax = -1e300;
    for (auto& p : corners) { double d = dot(p, n); vmin = std::min(vmin, d); vmax = std::max(vmax, d); }
    dmin = std::max(dmin, vmin);
    dmax = std::min(dmax, vmax);
    if (dmin > dmax) return;
    long long k0 = (long long)std::ceil(dmin / sp), k1 = (long long)std::floor(dmax / sp);
    if (k1 - k0 > 6000) return;
    std::vector<double> xs;
    for (long long k = k0; k <= k1; k++) {
        double d = k * sp;
        xs.clear();
        for (auto& l : loops) {
            size_t m = l.size();
            for (size_t i = 0; i < m; i++) {
                Vec2 a = l[i], b = l[(i + 1) % m];
                double da = dot(a, n) - d, db = dot(b, n) - d;
                if ((da > 0) != (db > 0)) xs.push_back(dot(a + (b - a) * (da / (da - db)), u));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            dl->AddLine(c.to_screen(n * d + u * xs[i]), c.to_screen(n * d + u * xs[i + 1]), col, th);
    }
}

static void hatch_solid(ImDrawList* dl, const Cad& c, const std::vector<std::vector<Vec2>>& loops, ImU32 col) {
    if (loops.size() == 1 && loops[0].size() >= 3) {
        std::vector<ImVec2> pts;
        for (auto& p : loops[0]) pts.push_back(c.to_screen(p));
        dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), col);
        return;
    }
    // Even-odd scanline fill in screen space (handles islands)
    std::vector<std::vector<ImVec2>> sl;
    float ymin = 1e30f, ymax = -1e30f;
    for (auto& l : loops) {
        std::vector<ImVec2> s;
        for (auto& p : l) { ImVec2 q = c.to_screen(p); s.push_back(q); ymin = std::min(ymin, q.y); ymax = std::max(ymax, q.y); }
        sl.push_back(std::move(s));
    }
    ymin = std::max(ymin, c.canvas_min.y);
    ymax = std::min(ymax, c.canvas_max.y);
    std::vector<float> xs;
    for (float y = std::floor(ymin) + 0.5f; y < ymax; y += 1.0f) {
        xs.clear();
        for (auto& s : sl)
            for (size_t i = 0, m = s.size(); i < m; i++) {
                ImVec2 a = s[i], b = s[(i + 1) % m];
                if ((a.y > y) != (b.y > y)) xs.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
            }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) dl->AddRectFilled(ImVec2(xs[i], y - 0.5f), ImVec2(xs[i + 1], y + 0.5f), col);
    }
}

static void draw_hatch(ImDrawList* dl, const Cad& c, const Entity& e, ImU32 col, float th) {
    if (e.loops.empty()) return;
    if (e.pat == HatchPat::Solid) { hatch_solid(dl, c, e.loops, col); return; }
    double sp = 3.175 * e.pscale;
    if (sp * c.zoom < 2.5) { hatch_solid(dl, c, e.loops, (col & 0x00FFFFFF) | 0x50000000); return; }
    hatch_lines(dl, c, e.loops, e.pangle + PI / 4, sp, col, th);
    if (e.pat == HatchPat::ANSI37) hatch_lines(dl, c, e.loops, e.pangle + 3 * PI / 4, sp, col, th);
}

static void draw_dim(ImDrawList* dl, const Cad& c, const Entity& e, ImU32 col, float th) {
    DimGeom g = dim_geom(c.doc, e);
    for (auto& [a, b] : g.lines) dl->AddLine(c.to_screen(a), c.to_screen(b), col, th);
    double asz = c.doc.dimstyle.asz * c.doc.dimstyle.scale;
    for (auto& ar : g.arrows) {
        Vec2 back = ar.tip - ar.dir * asz, side = perp(ar.dir) * (asz / 6);
        dl->AddTriangleFilled(c.to_screen(ar.tip), c.to_screen(back + side), c.to_screen(back - side), col);
    }
    Entity t = make_text({0, 0}, g.text_h, g.text_rot, g.text);
    double w = text_extent(t).x;
    draw_world_text(dl, c, g.text, g.text_pos - dir(g.text_rot) * (w / 2), g.text_h, g.text_rot, col, false, 0);
}

void draw_entity(ImDrawList* dl, const Cad& c, const Entity& e, ImU32 col, float th) {
    switch (e.type) {
    case EType::Line:
        dl->AddLine(c.to_screen(e.v[0].p), c.to_screen(e.v[1].p), col, th);
        break;
    case EType::Circle: case EType::Arc: case EType::Polyline:
        draw_segs(dl, c, entity_segs(e), is_closed_curve(e), col, th);
        break;
    case EType::Ellipse: {
        int n = arc_steps(len(e.major) * c.zoom, TAU);
        auto p = ellipse_points(e, std::max(n, 32));
        std::vector<ImVec2> s;
        for (auto& q : p) s.push_back(c.to_screen(q));
        dl->AddPolyline(s.data(), (int)s.size(), col, th, ImDrawFlags_Closed);
        break;
    }
    case EType::Text:
        draw_world_text(dl, c, e.text, e.v[0].p, e.height, e.rot, col, false, 0);
        break;
    case EType::MText:
        draw_world_text(dl, c, e.text, e.v[0].p, e.height, e.rot, col, true, e.width);
        break;
    case EType::Hatch:
        draw_hatch(dl, c, e, col, th);
        break;
    case EType::Dim:
        draw_dim(dl, c, e, col, th);
        break;
    }
}

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
static void draw_grid(ImDrawList* dl, const Cad& c) {
    double step = c.grid_step;
    if (step <= 0) return;
    while (step * c.zoom < 8) step *= 5;
    while (step * c.zoom > 400) step /= 5;
    Vec2 w0 = c.to_world(c.canvas_min), w1 = c.to_world(c.canvas_max);
    double x0 = std::min(w0.x, w1.x), x1 = std::max(w0.x, w1.x), y0 = std::min(w0.y, w1.y), y1 = std::max(w0.y, w1.y);
    long long i0 = (long long)std::floor(x0 / step), i1 = (long long)std::ceil(x1 / step);
    long long j0 = (long long)std::floor(y0 / step), j1 = (long long)std::ceil(y1 / step);
    for (int pass = 0; pass < 2; pass++) {
        for (long long i = i0; i <= i1; i++) {
            bool major = i % 5 == 0;
            if (major != (pass == 1)) continue;
            float x = c.to_screen({i * step, 0}).x;
            dl->AddLine(ImVec2(x, c.canvas_min.y), ImVec2(x, c.canvas_max.y), major ? COL_GRID_MAJOR : COL_GRID_MINOR);
        }
        for (long long j = j0; j <= j1; j++) {
            bool major = j % 5 == 0;
            if (major != (pass == 1)) continue;
            float y = c.to_screen({0, j * step}).y;
            dl->AddLine(ImVec2(c.canvas_min.x, y), ImVec2(c.canvas_max.x, y), major ? COL_GRID_MAJOR : COL_GRID_MINOR);
        }
    }
    ImVec2 o = c.to_screen({0, 0});
    dl->AddLine(ImVec2(o.x, c.canvas_min.y), ImVec2(o.x, c.canvas_max.y), IM_COL32(60, 110, 60, 255));
    dl->AddLine(ImVec2(c.canvas_min.x, o.y), ImVec2(c.canvas_max.x, o.y), IM_COL32(120, 55, 55, 255));
}

static void draw_ucs_icon(ImDrawList* dl, const Cad& c) {
    ImVec2 o(c.canvas_min.x + 38, c.canvas_max.y - 70);
    ImU32 xc = IM_COL32(230, 70, 70, 255), yc = IM_COL32(90, 200, 90, 255);
    dl->AddLine(o, ImVec2(o.x + 46, o.y), xc, 2.0f);
    dl->AddTriangleFilled(ImVec2(o.x + 52, o.y), ImVec2(o.x + 44, o.y - 4), ImVec2(o.x + 44, o.y + 4), xc);
    dl->AddLine(o, ImVec2(o.x, o.y - 46), yc, 2.0f);
    dl->AddTriangleFilled(ImVec2(o.x, o.y - 52), ImVec2(o.x - 4, o.y - 44), ImVec2(o.x + 4, o.y - 44), yc);
    dl->AddRect(ImVec2(o.x - 4, o.y - 4), ImVec2(o.x + 4, o.y + 4), IM_COL32(200, 200, 200, 255));
    dl->AddText(ImVec2(o.x + 54, o.y - 8), xc, "X");
    dl->AddText(ImVec2(o.x - 4, o.y - 72), yc, "Y");
}

static void draw_snap_marker(ImDrawList* dl, const Cad& c, const SnapInfo& s) {
    ImVec2 p = c.to_screen(s.p);
    float r = 7, th = 2;
    const char* name = "";
    switch (s.mode) {
    case OS_END: dl->AddRect(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), COL_SNAP, 0.0f, th); name = "Endpoint"; break;
    case OS_MID: dl->AddTriangle(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y + r * 0.8f), ImVec2(p.x - r, p.y + r * 0.8f), COL_SNAP, th); name = "Midpoint"; break;
    case OS_CEN: dl->AddCircle(p, r, COL_SNAP, 24, th); name = "Center"; break;
    case OS_QUA: dl->AddQuad(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), COL_SNAP, th); name = "Quadrant"; break;
    case OS_INT:
        dl->AddLine(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), COL_SNAP, th);
        dl->AddLine(ImVec2(p.x - r, p.y + r), ImVec2(p.x + r, p.y - r), COL_SNAP, th);
        name = "Intersection";
        break;
    case OS_PER:
        dl->AddLine(ImVec2(p.x - r, p.y + r), ImVec2(p.x + r, p.y + r), COL_SNAP, th);
        dl->AddLine(ImVec2(p.x - r, p.y + r), ImVec2(p.x - r, p.y - r), COL_SNAP, th);
        dl->AddRect(ImVec2(p.x - r, p.y), ImVec2(p.x, p.y + r), COL_SNAP, 0.0f, 1.5f);
        name = "Perpendicular";
        break;
    case OS_NEA:
        dl->AddLine(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), COL_SNAP, th);
        dl->AddLine(ImVec2(p.x - r, p.y + r), ImVec2(p.x + r, p.y - r), COL_SNAP, th);
        dl->AddLine(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y - r), COL_SNAP, th);
        dl->AddLine(ImVec2(p.x - r, p.y + r), ImVec2(p.x + r, p.y + r), COL_SNAP, th);
        name = "Nearest";
        break;
    case OS_INS:
        dl->AddRect(ImVec2(p.x - r, p.y - r), ImVec2(p.x + 2, p.y + 2), COL_SNAP, 0.0f, th);
        dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + r, p.y + r), COL_SNAP, 0.0f, th);
        name = "Insertion";
        break;
    }
    ImVec2 tp(p.x + 14, p.y + 12);
    ImVec2 ts = ImGui::CalcTextSize(name);
    dl->AddRectFilled(ImVec2(tp.x - 4, tp.y - 2), ImVec2(tp.x + ts.x + 4, tp.y + ts.y + 2), IM_COL32(255, 255, 225, 235), 2);
    dl->AddText(tp, IM_COL32(20, 20, 20, 255), name);
}

void Cad::render_scene(ImDrawList* dl) {
    dl->PushClipRect(canvas_min, canvas_max, true);
    dl->AddRectFilled(canvas_min, canvas_max, COL_BG);
    if (grid_on) draw_grid(dl, *this);

    for (int pass = 0; pass < 2; pass++)
        for (auto& e : doc.ents)
            if ((e.type == EType::Hatch) == (pass == 0)) draw_entity(dl, *this, e, aci_color(e.color), 1.0f);

    // Selection highlight
    for (auto& e : doc.ents)
        if (is_selected(e.id)) {
            if (e.type == EType::Hatch) {
                for (auto& l : e.loops) {
                    std::vector<ImVec2> pts;
                    for (auto& p : l) pts.push_back(to_screen(p));
                    dl->AddPolyline(pts.data(), (int)pts.size(), COL_SEL, 2.0f, ImDrawFlags_Closed);
                }
            } else draw_entity(dl, *this, e, COL_SEL, 2.0f);
        }
    if (hover_ent)
        if (const Entity* e = doc.find(hover_ent)) {
            ImU32 col = is_selected(e->id) ? COL_SEL : aci_color(e->color);
            if (e->type == EType::Hatch) draw_entity(dl, *this, *e, (col & 0x00FFFFFF) | 0x90000000, 2.0f);
            else draw_entity(dl, *this, *e, col, 3.0f);
        }

    // Grips
    if (!cmd || cmd_name == "GRIP") {
        for (auto id : sel) {
            const Entity* e = doc.find(id);
            if (!e) continue;
            for (auto& [key, gp] : grips(doc, *e)) {
                ImVec2 s = to_screen(gp);
                bool hot = hover_grip && near(gp, hover_grip_pos, 1e-9);
                ImU32 col = hot ? COL_GRIP_HOVER : COL_GRIP;
                if (cmd_name == "GRIP" && req.base && near(gp, *req.base, 1e-9)) col = IM_COL32(255, 30, 30, 255);
                dl->AddRectFilled(ImVec2(s.x - 4.5f, s.y - 4.5f), ImVec2(s.x + 4.5f, s.y + 4.5f), col);
                dl->AddRect(ImVec2(s.x - 4.5f, s.y - 4.5f), ImVec2(s.x + 4.5f, s.y + 4.5f), IM_COL32(10, 20, 40, 255));
            }
        }
    }

    bool point_mode = req.kind == ReqKind::Point || req.kind == ReqKind::Distance || req.kind == ReqKind::Angle;
    // Rubber band & previews
    if (req.base && point_mode && req.rubber && canvas_hovered)
        dashed_line(dl, to_screen(*req.base), to_screen(cursor), IM_COL32(220, 220, 220, 200), 1.0f);
    if (req.preview && (canvas_hovered || req.kind == ReqKind::String)) {
        for (auto& g : req.preview(cursor)) draw_entity(dl, *this, g, aci_color(g.color, 0.9f), 1.0f);
    }

    // Selection window
    if (win_start) {
        ImVec2 a = to_screen(*win_start), b = mouse_pos;
        bool crossing = b.x < a.x;
        ImVec2 mn(std::min(a.x, b.x), std::min(a.y, b.y)), mx(std::max(a.x, b.x), std::max(a.y, b.y));
        dl->AddRectFilled(mn, mx, crossing ? IM_COL32(60, 180, 80, 60) : IM_COL32(60, 110, 230, 60));
        ImU32 bc = crossing ? IM_COL32(110, 230, 130, 255) : IM_COL32(110, 160, 255, 255);
        if (crossing) {
            dashed_line(dl, mn, ImVec2(mx.x, mn.y), bc, 1); dashed_line(dl, ImVec2(mx.x, mn.y), mx, bc, 1);
            dashed_line(dl, mx, ImVec2(mn.x, mx.y), bc, 1); dashed_line(dl, ImVec2(mn.x, mx.y), mn, bc, 1);
        } else dl->AddRect(mn, mx, bc);
    }

    if (snap.valid() && canvas_hovered) draw_snap_marker(dl, *this, snap);
    draw_ucs_icon(dl, *this);

    // Crosshair cursor
    if (canvas_hovered) {
        ImVec2 m = to_screen(cursor);
        if (!point_mode) m = mouse_pos;
        float half = std::max(24.0f, (canvas_max.x - canvas_min.x) * 0.025f);
        ImU32 cc = IM_COL32(235, 235, 235, 255);
        bool pickbox = !point_mode;
        bool show_cross = req.kind != ReqKind::Pick && req.kind != ReqKind::Select;
        if (show_cross) {
            dl->AddLine(ImVec2(m.x - half, m.y), ImVec2(m.x + half, m.y), cc);
            dl->AddLine(ImVec2(m.x, m.y - half), ImVec2(m.x, m.y + half), cc);
        }
        if (pickbox) dl->AddRect(ImVec2(m.x - 5, m.y - 5), ImVec2(m.x + 5, m.y + 5), cc);
    }
    dl->PopClipRect();
}

// ---------------------------------------------------------------------------
// Ribbon icons (vector drawn)
// ---------------------------------------------------------------------------
void draw_icon(ImDrawList* dl, const char* id, ImVec2 p, float s, bool active) {
    const ImU32 W = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 230, 238, 255);
    const ImU32 B = IM_COL32(80, 170, 255, 255);
    const ImU32 R = IM_COL32(240, 90, 80, 255);
    const ImU32 Y = IM_COL32(245, 200, 70, 255);
    auto P = [&](float x, float y) { return ImVec2(p.x + x * s, p.y + y * s); };
    auto dot_ = [&](float x, float y, ImU32 c) { ImVec2 q = P(x, y); dl->AddRectFilled(ImVec2(q.x - 2, q.y - 2), ImVec2(q.x + 2, q.y + 2), c); };
    const float t = 1.6f;
    std::string n = id;
    if (n == "line") { dl->AddLine(P(0.15f, 0.85f), P(0.85f, 0.15f), W, t); dot_(0.15f, 0.85f, B); dot_(0.85f, 0.15f, B); }
    else if (n == "pline") { dl->AddLine(P(0.1f, 0.8f), P(0.35f, 0.3f), W, t); dl->AddLine(P(0.35f, 0.3f), P(0.65f, 0.7f), W, t); dl->PathArcTo(P(0.78f, 0.55f), 0.18f * s, 2.3f, 5.5f); dl->PathStroke(W, t); dot_(0.1f, 0.8f, B); dot_(0.35f, 0.3f, B); dot_(0.65f, 0.7f, B); }
    else if (n == "circle") { dl->AddCircle(P(0.5f, 0.5f), 0.36f * s, W, 32, t); dot_(0.5f, 0.5f, B); dl->AddLine(P(0.5f, 0.5f), P(0.86f, 0.5f), B, 1); }
    else if (n == "arc") { dl->PathArcTo(P(0.5f, 0.7f), 0.38f * s, 3.14159f, 6.28318f); dl->PathStroke(W, t); dot_(0.12f, 0.7f, B); dot_(0.88f, 0.7f, B); dot_(0.5f, 0.32f, B); }
    else if (n == "rect") { dl->AddRect(P(0.12f, 0.25f), P(0.88f, 0.75f), W, 0.0f, t); dot_(0.12f, 0.75f, B); dot_(0.88f, 0.25f, B); }
    else if (n == "polygon") { ImVec2 pts[6]; for (int i = 0; i < 6; i++) pts[i] = P(0.5f + 0.38f * std::cos(i * 1.0472f), 0.5f + 0.38f * std::sin(i * 1.0472f)); dl->AddPolyline(pts, 6, W, t, ImDrawFlags_Closed); dot_(0.5f, 0.5f, B); }
    else if (n == "ellipse") { dl->AddEllipse(P(0.5f, 0.5f), ImVec2(0.4f * s, 0.24f * s), W, 0, 32, t); dot_(0.5f, 0.5f, B); }
    else if (n == "hatch") {
        dl->AddRect(P(0.12f, 0.12f), P(0.88f, 0.88f), W, 0.0f, t);
        dl->PushClipRect(P(0.12f, 0.12f), P(0.88f, 0.88f), true);
        for (float k = -0.8f; k < 1.0f; k += 0.18f) dl->AddLine(P(k, 0.88f), P(k + 0.76f, 0.12f), B, 1.2f);
        dl->PopClipRect();
    }
    else if (n == "mtext") { dl->AddText(ImGui::GetFont(), s * 0.62f, P(0.08f, 0.05f), W, "A"); for (int i = 0; i < 3; i++) dl->AddLine(P(0.5f, 0.25f + i * 0.2f), P(0.9f, 0.25f + i * 0.2f), B, 1.5f); dl->AddLine(P(0.1f, 0.85f), P(0.9f, 0.85f), B, 1.5f); }
    else if (n == "text") { dl->AddText(ImGui::GetFont(), s * 0.8f, P(0.22f, 0.05f), W, "A"); dl->AddLine(P(0.1f, 0.88f), P(0.9f, 0.88f), B, 1.5f); }
    else if (n == "dimlin" || n == "dimali") {
        bool al = n == "dimali";
        ImVec2 a = P(0.12f, al ? 0.8f : 0.7f), b = P(0.88f, al ? 0.35f : 0.7f);
        ImVec2 off = al ? ImVec2(-0.12f * s, -0.2f * s) : ImVec2(0, -0.35f * s);
        dl->AddLine(a, ImVec2(a.x + off.x * 1.2f, a.y + off.y * 1.2f), W, 1);
        dl->AddLine(b, ImVec2(b.x + off.x * 1.2f, b.y + off.y * 1.2f), W, 1);
        ImVec2 da(a.x + off.x, a.y + off.y), db(b.x + off.x, b.y + off.y);
        dl->AddLine(da, db, W, t);
        dl->AddCircleFilled(da, 2.5f, B); dl->AddCircleFilled(db, 2.5f, B);
        dl->AddText(ImGui::GetFont(), s * 0.34f, P(0.36f, al ? 0.12f : 0.08f), Y, "12");
    }
    else if (n == "dimrad" || n == "dimdia") {
        dl->AddCircle(P(0.5f, 0.55f), 0.34f * s, W, 32, 1.2f);
        if (n == "dimrad") dl->AddLine(P(0.5f, 0.55f), P(0.74f, 0.31f), B, t);
        else dl->AddLine(P(0.26f, 0.79f), P(0.74f, 0.31f), B, t);
        dl->AddText(ImGui::GetFont(), s * 0.34f, P(0.02f, 0.0f), Y, n == "dimrad" ? "R" : "\xC3\x98");
    }
    else if (n == "move") {
        dl->AddLine(P(0.5f, 0.1f), P(0.5f, 0.9f), W, t); dl->AddLine(P(0.1f, 0.5f), P(0.9f, 0.5f), W, t);
        dl->AddTriangleFilled(P(0.5f, 0.05f), P(0.4f, 0.2f), P(0.6f, 0.2f), W); dl->AddTriangleFilled(P(0.5f, 0.95f), P(0.4f, 0.8f), P(0.6f, 0.8f), W);
        dl->AddTriangleFilled(P(0.05f, 0.5f), P(0.2f, 0.4f), P(0.2f, 0.6f), W); dl->AddTriangleFilled(P(0.95f, 0.5f), P(0.8f, 0.4f), P(0.8f, 0.6f), W);
    }
    else if (n == "copy") { dl->AddRect(P(0.1f, 0.35f), P(0.6f, 0.85f), W, 0.0f, t); dl->AddRect(P(0.4f, 0.12f), P(0.9f, 0.62f), B, 0.0f, t); }
    else if (n == "rotate") { dl->PathArcTo(P(0.5f, 0.5f), 0.34f * s, 0.6f, 5.4f); dl->PathStroke(W, t); dl->AddTriangleFilled(P(0.8f, 0.18f), P(0.9f, 0.42f), P(0.66f, 0.36f), W); dot_(0.5f, 0.5f, B); }
    else if (n == "mirror") { dl->AddTriangle(P(0.08f, 0.8f), P(0.42f, 0.8f), P(0.42f, 0.25f), W, t); dl->AddTriangle(P(0.92f, 0.8f), P(0.58f, 0.8f), P(0.58f, 0.25f), B, t); dashed_line(dl, P(0.5f, 0.05f), P(0.5f, 0.95f), Y, 1, 3, 2); }
    else if (n == "scale") { dl->AddRect(P(0.1f, 0.55f), P(0.45f, 0.9f), W, 0.0f, t); dl->AddRect(P(0.1f, 0.1f), P(0.9f, 0.9f), B, 0.0f, 1.2f); dl->AddLine(P(0.45f, 0.55f), P(0.85f, 0.15f), W, 1); }
    else if (n == "trim") { dl->AddLine(P(0.55f, 0.05f), P(0.55f, 0.95f), B, t); dl->AddLine(P(0.08f, 0.5f), P(0.55f, 0.5f), W, t); dashed_line(dl, P(0.55f, 0.5f), P(0.95f, 0.5f), R, t, 3, 3); }
    else if (n == "extend") { dl->AddLine(P(0.85f, 0.05f), P(0.85f, 0.95f), B, t); dl->AddLine(P(0.08f, 0.5f), P(0.45f, 0.5f), W, t); dashed_line(dl, P(0.45f, 0.5f), P(0.85f, 0.5f), Y, t, 3, 3); dl->AddTriangleFilled(P(0.85f, 0.5f), P(0.72f, 0.42f), P(0.72f, 0.58f), Y); }
    else if (n == "join") { dl->AddLine(P(0.08f, 0.7f), P(0.45f, 0.35f), W, t); dl->PathArcTo(P(0.65f, 0.55f), 0.28f * s, 3.9f, 6.2f); dl->PathStroke(W, t); dl->AddCircleFilled(P(0.45f, 0.35f), 3.5f, B); }
    else if (n == "explode") { for (int i = 0; i < 8; i++) { float a = i * 0.785f; dl->AddLine(P(0.5f + 0.15f * std::cos(a), 0.5f + 0.15f * std::sin(a)), P(0.5f + 0.42f * std::cos(a), 0.5f + 0.42f * std::sin(a)), i % 2 ? W : Y, t); } }
    else if (n == "erase") { dl->AddRect(P(0.15f, 0.3f), P(0.85f, 0.7f), W, 2.0f, t); dl->AddLine(P(0.2f, 0.15f), P(0.8f, 0.85f), R, 2.2f); dl->AddLine(P(0.8f, 0.15f), P(0.2f, 0.85f), R, 2.2f); }
    else if (n == "group" || n == "ungroup") {
        dl->AddCircle(P(0.35f, 0.4f), 0.16f * s, W, 20, t); dl->AddRect(P(0.45f, 0.45f), P(0.8f, 0.8f), W, 0.0f, t);
        ImU32 bc = n == "group" ? B : R;
        dashed_line(dl, P(0.08f, 0.1f), P(0.92f, 0.1f), bc, 1, 3, 2); dashed_line(dl, P(0.92f, 0.1f), P(0.92f, 0.92f), bc, 1, 3, 2);
        dashed_line(dl, P(0.92f, 0.92f), P(0.08f, 0.92f), bc, 1, 3, 2); dashed_line(dl, P(0.08f, 0.92f), P(0.08f, 0.1f), bc, 1, 3, 2);
    }
    else if (n == "zoomext") { dl->AddCircle(P(0.42f, 0.42f), 0.26f * s, W, 24, t); dl->AddLine(P(0.6f, 0.6f), P(0.9f, 0.9f), W, 2.6f); dl->AddRect(P(0.3f, 0.3f), P(0.54f, 0.54f), B, 0.0f, 1); }
    else if (n == "undo" || n == "redo") {
        bool u = n == "undo";
        dl->PathArcTo(P(0.5f, 0.6f), 0.3f * s, u ? 3.6f : -0.46f, u ? 6.6f : 2.6f); dl->PathStroke(W, t);
        if (u) dl->AddTriangleFilled(P(0.1f, 0.45f), P(0.32f, 0.3f), P(0.3f, 0.55f), W);
        else dl->AddTriangleFilled(P(0.9f, 0.45f), P(0.68f, 0.3f), P(0.7f, 0.55f), W);
    }
    else if (n == "props") { dl->AddRect(P(0.15f, 0.1f), P(0.85f, 0.9f), W, 2.0f, t); for (int i = 0; i < 4; i++) { dl->AddLine(P(0.25f, 0.25f + i * 0.17f), P(0.45f, 0.25f + i * 0.17f), B, 1.5f); dl->AddLine(P(0.5f, 0.25f + i * 0.17f), P(0.75f, 0.25f + i * 0.17f), W, 1.5f); } }
    else { dl->AddRect(P(0.2f, 0.2f), P(0.8f, 0.8f), W); }
}

} // namespace cad
