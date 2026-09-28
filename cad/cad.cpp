#include "cad.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <set>

namespace cad {

Task<> grip_edit(Cad& c, std::vector<std::pair<uint32_t, PKey>> grips, Vec2 base);

// ---------------------------------------------------------------------------
// Coroutine plumbing
// ---------------------------------------------------------------------------
void InputAwait::await_suspend(std::coroutine_handle<> h) {
    c->req = std::move(r);
    c->waiting = h;
}
Input InputAwait::await_resume() { return c->result; }

static Req make_req(ReqKind k, std::string prompt, std::optional<Vec2> base = {}, PreviewFn pv = {}) {
    Req r;
    r.kind = k;
    r.prompt = std::move(prompt);
    r.base = base;
    r.preview = std::move(pv);
    return r;
}

InputAwait Cad::get_point(std::string prompt, std::optional<Vec2> base, PreviewFn preview, bool rubber) {
    Req r = make_req(ReqKind::Point, std::move(prompt), base, std::move(preview));
    r.rubber = rubber;
    return ask(std::move(r));
}
InputAwait Cad::get_distance(std::string prompt, std::optional<Vec2> base, PreviewFn preview) {
    return ask(make_req(ReqKind::Distance, std::move(prompt), base, std::move(preview)));
}
InputAwait Cad::get_angle(std::string prompt, std::optional<Vec2> base, PreviewFn preview) {
    return ask(make_req(ReqKind::Angle, std::move(prompt), base, std::move(preview)));
}
InputAwait Cad::get_integer(std::string prompt) { return ask(make_req(ReqKind::Integer, std::move(prompt))); }
InputAwait Cad::get_real(std::string prompt) { return ask(make_req(ReqKind::Real, std::move(prompt))); }
InputAwait Cad::get_keyword(std::string prompt) { return ask(make_req(ReqKind::Keyword, std::move(prompt))); }
InputAwait Cad::get_string(std::string prompt, PreviewFn preview) {
    return ask(make_req(ReqKind::String, std::move(prompt), {}, std::move(preview)));
}
InputAwait Cad::get_pick(std::string prompt) { return ask(make_req(ReqKind::Pick, std::move(prompt))); }

Task<std::vector<uint32_t>> Cad::select_objects(std::string prompt) {
    if (!pre_sel.empty()) {
        auto s = std::move(pre_sel);
        pre_sel.clear();
        print(prompt + " " + std::to_string(s.size()) + " found");
        co_return s;
    }
    cmd_sel.clear();
    Input in = co_await ask(make_req(ReqKind::Select, prompt));
    auto s = std::move(cmd_sel);
    cmd_sel.clear();
    (void)in;
    co_return s;
}

Cad::Cad() {
    log.push_back("MiniCAD - type a command (e.g. LINE or L) and press Enter or Space.");
}

bool Cad::is_selected(uint32_t id) const {
    return std::find(sel.begin(), sel.end(), id) != sel.end() ||
           std::find(cmd_sel.begin(), cmd_sel.end(), id) != cmd_sel.end();
}

std::vector<uint32_t> Cad::expand_groups(const std::vector<uint32_t>& ids) const {
    if (!group_sel) return ids;
    std::set<uint32_t> groups;
    for (auto id : ids) if (auto* e = doc.find(id); e && e->group) groups.insert(e->group);
    std::vector<uint32_t> out = ids;
    for (auto& e : doc.ents)
        if (e.group && groups.count(e.group) && std::find(out.begin(), out.end(), e.id) == out.end()) out.push_back(e.id);
    return out;
}

void Cad::print(const std::string& s) {
    log.push_back(s);
    if (log.size() > 500) log.erase(log.begin(), log.begin() + 100);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
static std::string upper(std::string s) {
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const CmdDef* find_command(const std::string& name) {
    std::string n = upper(trim(name));
    if (!n.empty() && (n[0] == '_' || n[0] == '.')) n = n.substr(1);
    for (auto& d : command_table()) {
        if (n == d.name) return &d;
        for (auto a : d.aliases) if (n == a) return &d;
    }
    return nullptr;
}

void Cad::start_command(const std::string& name) {
    const CmdDef* def = find_command(name);
    if (!def) {
        print("Unknown command \"" + upper(trim(name)) + "\".  Press F1 for help.");
        return;
    }
    cancel(true);
    pre_sel = sel;
    sel.clear();
    win_start.reset();
    cmd_checkpointed = false;
    cmd_name = def->name;
    if (std::string(def->name) != "U" && std::string(def->name) != "REDO") last_cmd = def->name;
    print("Command: " + std::string(def->name));
    cmd.emplace(def->fn(*this));
    auto h = cmd->h;
    h.resume();
    if (cmd && cmd->h.done()) { cmd.reset(); cmd_name.clear(); req = {}; pre_sel.clear(); }
}

void Cad::cancel(bool quiet) {
    if (cmd) {
        waiting = {};
        req = {};
        cmd.reset();
        cmd_name.clear();
        if (!quiet) print("*Cancel*");
    }
    cmd_sel.clear();
    pre_sel.clear();
    osnap_override = 0;
    win_start.reset();
    input.clear();
}

void Cad::resume(Input in) {
    ReqKind k = req.kind;
    if (in.st == Input::Ok && (k == ReqKind::Point)) last_point = in.p;
    result = std::move(in);
    osnap_override = 0;
    win_start.reset();
    auto h = waiting;
    waiting = {};
    req = {};
    if (h) h.resume();
    if (cmd && cmd->h.done()) {
        cmd.reset();
        cmd_name.clear();
        req = {};
        pre_sel.clear();
        cmd_sel.clear();
    }
}

// ---------------------------------------------------------------------------
// Parsing typed input
// ---------------------------------------------------------------------------
std::vector<std::string> prompt_keywords(const std::string& prompt) {
    std::vector<std::string> out;
    size_t a = prompt.find('['), b = prompt.find(']', a == std::string::npos ? 0 : a);
    if (a == std::string::npos || b == std::string::npos) return out;
    std::string inner = prompt.substr(a + 1, b - a - 1);
    size_t start = 0;
    while (start <= inner.size()) {
        size_t sl = inner.find('/', start);
        std::string k = inner.substr(start, sl == std::string::npos ? std::string::npos : sl - start);
        if (!k.empty()) out.push_back(k);
        if (sl == std::string::npos) break;
        start = sl + 1;
    }
    return out;
}

static std::string kw_alias(const std::string& kw) {
    std::string a;
    for (char ch : kw) if (std::isupper((unsigned char)ch) || std::isdigit((unsigned char)ch)) a += ch;
    return a.empty() ? upper(kw) : a;
}

std::string match_keyword(const std::string& prompt, const std::string& input_raw) {
    std::string in;
    for (char ch : upper(trim(input_raw))) if (ch != ' ') in += ch;
    if (in.empty()) return {};
    for (auto& kw : prompt_keywords(prompt)) {
        std::string full;
        for (char ch : kw) if (ch != ' ') full += (char)std::toupper((unsigned char)ch);
        std::string alias = kw_alias(kw);
        if (in == alias || in == full) return kw;
        if (in.size() >= alias.size() && full.rfind(in, 0) == 0 && full.rfind(alias, 0) == 0) return kw;
    }
    return {};
}

bool parse_number(const std::string& s_raw, double& v) {
    std::string s = trim(s_raw);
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == 0;
}

bool parse_point(const std::string& s_raw, Vec2 last, Vec2& out) {
    std::string s = trim(s_raw);
    if (s.empty()) return false;
    bool rel = false;
    if (s[0] == '@') { rel = true; s = s.substr(1); if (trim(s).empty()) { out = last; return true; } }
    else if (s[0] == '#') s = s.substr(1);
    size_t lt = s.find('<');
    if (lt != std::string::npos) {
        double d, a;
        if (!parse_number(s.substr(0, lt), d) || !parse_number(s.substr(lt + 1), a)) return false;
        Vec2 v = dir(a * PI / 180.0) * d;
        out = rel ? last + v : v;
        return true;
    }
    size_t cm = s.find(',');
    if (cm == std::string::npos) return false;
    std::string ys = s.substr(cm + 1);
    size_t cm2 = ys.find(',');
    if (cm2 != std::string::npos) ys = ys.substr(0, cm2); // ignore Z
    double x, y;
    if (!parse_number(s.substr(0, cm), x) || !parse_number(ys, y)) return false;
    out = rel ? last + Vec2{x, y} : Vec2{x, y};
    return true;
}

static uint32_t osnap_token(const std::string& t) {
    std::string u = upper(t);
    if (u == "END" || u == "ENDP") return OS_END;
    if (u == "MID") return OS_MID;
    if (u == "CEN") return OS_CEN;
    if (u == "QUA" || u == "QUAD") return OS_QUA;
    if (u == "INT") return OS_INT;
    if (u == "PER") return OS_PER;
    if (u == "NEA") return OS_NEA;
    if (u == "INS") return OS_INS;
    if (u == "NON" || u == "NONE") return 0x8000;
    return 0;
}

void Cad::submit(const std::string& raw) {
    std::string t = trim(raw);
    if (!t.empty() && (history.empty() || history.back() != t)) history.push_back(t);
    history_pos = -1;

    if (!cmd) {
        if (t.empty()) { if (!last_cmd.empty()) start_command(last_cmd); return; }
        start_command(t);
        return;
    }

    ReqKind k = req.kind;
    std::string echo = req.prompt;
    if (k != ReqKind::Select) print(echo + " " + (k == ReqKind::String ? raw : t));

    bool pointish = k == ReqKind::Point || k == ReqKind::Distance || k == ReqKind::Angle;
    if (pointish) {
        if (uint32_t os = osnap_token(t)) {
            osnap_override = os;
            print(std::string("of"));
            return;
        }
    }

    Input in;
    if (k == ReqKind::MText) { // typed instead of using the editor; \P = new paragraph
        if (raw.empty()) { resume(in); return; }
        std::string s = raw;
        for (size_t pos; (pos = s.find("\\P")) != std::string::npos;) s.replace(pos, 2, "\n");
        in.st = Input::Ok; in.s = s; resume(in); return;
    }
    if (k == ReqKind::String) {
        if (raw.empty()) { in.st = Input::None; resume(in); return; }
        in.st = Input::Ok; in.s = raw; resume(in); return;
    }

    std::string kw = match_keyword(req.prompt, t);
    if (!kw.empty()) { in.st = Input::Keyword; in.s = kw; resume(in); return; }

    if (t.empty()) {
        if (k == ReqKind::Select) { print(std::to_string(cmd_sel.size()) + " found"); resume(in); return; }
        if (req.allow_none) { in.st = Input::None; resume(in); return; }
        print("Requires a value.");
        return;
    }

    switch (k) {
    case ReqKind::Point: {
        Vec2 p;
        double v;
        if (parse_point(t, last_point, p)) { in.st = Input::Ok; in.p = p; resume(in); return; }
        if (req.base && parse_number(t, v)) { // direct distance entry
            Vec2 d = cursor - *req.base;
            if (len(d) < EPS) d = {1, 0};
            in.st = Input::Ok; in.p = *req.base + norm(d) * v; resume(in); return;
        }
        print("Point or option keyword required.");
        return;
    }
    case ReqKind::Distance: {
        double v; Vec2 p;
        if (parse_number(t, v)) { in.st = Input::Ok; in.v = v; resume(in); return; }
        if (req.base && parse_point(t, last_point, p)) { in.st = Input::Ok; in.v = dist(*req.base, p); resume(in); return; }
        print("Requires numeric distance, second point, or option keyword.");
        return;
    }
    case ReqKind::Angle: {
        double v;
        if (parse_number(t, v)) { in.st = Input::Ok; in.v = v; resume(in); return; }
        print("Requires valid numeric angle or second point.");
        return;
    }
    case ReqKind::Integer: {
        double v;
        if (parse_number(t, v) && v == std::floor(v)) { in.st = Input::Ok; in.v = v; resume(in); return; }
        print("Requires an integer value.");
        return;
    }
    case ReqKind::Real: {
        double v;
        if (parse_number(t, v)) { in.st = Input::Ok; in.v = v; resume(in); return; }
        print("Requires a numeric value.");
        return;
    }
    case ReqKind::Select: {
        std::string u = upper(t);
        if (u == "ALL") {
            for (auto& e : doc.ents) if (!is_selected(e.id)) cmd_sel.push_back(e.id);
            print(echo + " ALL  " + std::to_string(cmd_sel.size()) + " found");
            return;
        }
        print("*Invalid selection*");
        return;
    }
    case ReqKind::Keyword: print("Invalid option keyword."); return;
    default: print("Invalid input."); return;
    }
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------
Snapshot Cad::snapshot() const { return Snapshot{doc.ents, doc.next_id, doc.next_group, doc.dimstyle}; }

void Cad::restore(const Snapshot& s) {
    doc.ents = s.ents;
    doc.next_id = std::max(doc.next_id, s.next_id);
    doc.next_group = std::max(doc.next_group, s.next_group);
    doc.dimstyle = s.dimstyle;
    doc.touch();
    sel.clear();
}

void Cad::force_checkpoint() {
    undo_stack.push_back(snapshot());
    if (undo_stack.size() > 200) undo_stack.erase(undo_stack.begin());
    redo_stack.clear();
}

void Cad::checkpoint() {
    if (cmd && cmd_checkpointed) return;
    force_checkpoint();
    if (cmd) cmd_checkpointed = true;
}

void Cad::undo() {
    if (undo_stack.empty()) { print("Everything has been undone"); return; }
    redo_stack.push_back(snapshot());
    restore(undo_stack.back());
    undo_stack.pop_back();
    print("Undo");
}

void Cad::redo() {
    if (redo_stack.empty()) { print("Nothing to redo"); return; }
    undo_stack.push_back(snapshot());
    restore(redo_stack.back());
    redo_stack.pop_back();
    print("Redo");
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------
ImVec2 Cad::to_screen(Vec2 p) const {
    float cx = (canvas_min.x + canvas_max.x) * 0.5f, cy = (canvas_min.y + canvas_max.y) * 0.5f;
    return ImVec2(cx + (float)((p.x - view_center.x) * zoom), cy - (float)((p.y - view_center.y) * zoom));
}
Vec2 Cad::to_world(ImVec2 s) const {
    double cx = (canvas_min.x + canvas_max.x) * 0.5, cy = (canvas_min.y + canvas_max.y) * 0.5;
    return {view_center.x + (s.x - cx) / zoom, view_center.y - (s.y - cy) / zoom};
}

void Cad::zoom_window(Vec2 a, Vec2 b) {
    double w = std::fabs(b.x - a.x), h = std::fabs(b.y - a.y);
    double cw = std::max(1.0f, canvas_max.x - canvas_min.x), ch = std::max(1.0f, canvas_max.y - canvas_min.y);
    if (w < EPS && h < EPS) return;
    view_center = (a + b) * 0.5;
    zoom = std::min(w > EPS ? cw / w : 1e9, h > EPS ? ch / h : 1e9);
    zoom = std::clamp(zoom, 1e-6, 1e7);
}

void Cad::zoom_extents() {
    BBox b;
    for (auto& e : doc.ents) b.add(bbox(doc, e));
    if (!b.valid()) { b.add({0, 0}); b.add({420, 297}); }
    Vec2 pad = (b.mx - b.mn) * 0.05 + Vec2{1e-3, 1e-3};
    zoom_window(b.mn - pad, b.mx + pad);
    // Keep the drawing clear of the floating command line at the bottom of the canvas
    const double reserve = 110.0;
    double ch = canvas_max.y - canvas_min.y;
    if (ch > reserve * 2) {
        zoom *= (ch - reserve) / ch;
        view_center.y -= reserve * 0.5 / zoom;
    }
}

// ---------------------------------------------------------------------------
// Picking & snapping
// ---------------------------------------------------------------------------
uint32_t Cad::entity_at(Vec2 p) const {
    double tol = pick_tol();
    uint32_t best = 0;
    double bd = 1e300;
    for (auto it = doc.ents.rbegin(); it != doc.ents.rend(); ++it) {
        double dd = hit_dist(doc, *it, p, tol);
        if (it->type == EType::Hatch && dd == 0) dd = tol * 0.95; // lines on top of hatches win
        if (dd <= tol && dd < bd) { bd = dd; best = it->id; }
    }
    return best;
}

void Cad::apply_ortho_grid(Vec2& p) const {
    if (snap_on && snap_step > 0) p = {std::round(p.x / snap_step) * snap_step, std::round(p.y / snap_step) * snap_step};
    if (ortho_on && req.base) {
        Vec2 d = p - *req.base;
        p = std::fabs(d.x) >= std::fabs(d.y) ? Vec2{p.x, req.base->y} : Vec2{req.base->x, p.y};
    }
}

void Cad::compute_snap() {
    snap = {};
    uint32_t modes = osnap_override ? osnap_override : (osnap_on ? osnap_modes : 0);
    if (modes == 0 || modes == 0x8000 || !req.snap) return;
    const Vec2 p = raw_cursor;
    const double ap = 10.0 / zoom;
    double best = 1e300, best_near = 1e300;
    SnapInfo near_snap;
    auto consider = [&](uint32_t mode, Vec2 q, uint32_t ent, PKey key) {
        if (!(modes & mode)) return;
        double dd = dist(p, q);
        if (dd > ap) return;
        if (mode == OS_NEA) {
            if (dd < best_near) { best_near = dd; near_snap = {mode, q, ent, key}; }
            return;
        }
        if (dd < best) { best = dd; snap = {mode, q, ent, key}; }
    };

    std::vector<const Entity*> nearby;
    for (auto& e : doc.ents) {
        if (e.type == EType::Hatch || e.type == EType::Dim) continue;
        BBox b = bbox(doc, e);
        b.mn -= Vec2{ap, ap}; b.mx += Vec2{ap, ap};
        if (!b.contains(p)) continue;

        if (e.type == EType::Text || e.type == EType::MText) {
            consider(OS_INS, e.v[0].p, e.id, {KeyKind::Vertex, 0});
            continue;
        }
        auto segs = entity_segs(e);
        bool close_to_curve = false;
        int n = (int)segs.size();
        for (int i = 0; i < n; i++) {
            const Seg& s = segs[i];
            double sd = seg_dist(s, p);
            if (sd <= ap) close_to_curve = true;
            if (e.type != EType::Ellipse) {
                PKey ka, kb;
                if (e.type == EType::Line) { ka = {KeyKind::Vertex, 0}; kb = {KeyKind::Vertex, 1}; }
                else if (e.type == EType::Arc) { ka = {KeyKind::Vertex, 0}; kb = {KeyKind::Vertex, 1}; }
                else if (e.type == EType::Polyline) { ka = {KeyKind::Vertex, i}; kb = {KeyKind::Vertex, (i + 1) % (int)e.v.size()}; }
                if (e.type != EType::Circle) {
                    consider(OS_END, s.a, e.id, ka);
                    consider(OS_END, s.b, e.id, kb);
                    consider(OS_MID, s.at(0.5), e.id, {KeyKind::Mid, i});
                }
                if (s.arc && e.type == EType::Polyline && (sd <= ap || dist(s.c, p) <= ap)) consider(OS_CEN, s.c, e.id, {KeyKind::Center, i});
            }
            if (sd <= ap) consider(OS_NEA, s.at(seg_closest_t(s, p)), e.id, {});
            if (req.base && (modes & OS_PER)) {
                Vec2 foot;
                if (!s.arc) { Vec2 dd = s.b - s.a; double l2 = dot(dd, dd); if (l2 < EPS) continue; foot = s.a + dd * (dot(*req.base - s.a, dd) / l2); }
                else foot = s.c + norm(*req.base - s.c) * s.r;
                consider(OS_PER, foot, e.id, {});
            }
        }
        if (e.type == EType::Circle || e.type == EType::Arc || e.type == EType::Ellipse) {
            if (close_to_curve || dist(e.c, p) <= ap) consider(OS_CEN, e.c, e.id, {KeyKind::Center, 0});
            for (int q = 0; q < 4; q++) {
                Vec2 qp;
                if (!key_point(doc, e, {KeyKind::Quad, q}, qp)) continue;
                if (e.type == EType::Arc) {
                    Seg s = segs[0];
                    double t = seg_param(s, qp);
                    if (t < -1e-9 || t > 1 + 1e-9) continue;
                }
                consider(OS_QUA, qp, e.id, {KeyKind::Quad, q});
            }
        }
        if (close_to_curve) nearby.push_back(&e);
    }

    if ((modes & OS_INT) && nearby.size() >= 1) {
        for (size_t i = 0; i < nearby.size(); i++) {
            auto A = entity_segs(*nearby[i]);
            for (size_t j = i; j < nearby.size(); j++) {
                auto B = (i == j) ? A : entity_segs(*nearby[j]);
                for (size_t a = 0; a < A.size(); a++)
                    for (size_t b = (i == j ? a + 2 : 0); b < B.size(); b++) {
                        std::vector<Hit> hits;
                        seg_intersect(A[a], B[b], hits, 1e-9);
                        for (auto& h : hits) consider(OS_INT, h.p, 0, {});
                    }
            }
        }
    }
    if (!snap.valid() && near_snap.valid()) snap = near_snap;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------
void Cad::select_click(Vec2 p, bool shift) {
    uint32_t id = entity_at(p);
    if (!id) return;
    auto ids = expand_groups({id});
    auto& target = (req.kind == ReqKind::Select) ? cmd_sel : sel;
    size_t before = target.size();
    for (auto x : ids) {
        auto it = std::find(target.begin(), target.end(), x);
        if (shift) { if (it != target.end()) target.erase(it); }
        else if (it == target.end()) target.push_back(x);
    }
    if (req.kind == ReqKind::Select) {
        if (shift) print(req.prompt + " " + std::to_string(before - target.size()) + " removed, " + std::to_string(target.size()) + " total");
        else print(req.prompt + " " + std::to_string(target.size() - before) + " found, " + std::to_string(target.size()) + " total");
    }
}

void Cad::select_window(Vec2 a, Vec2 b, bool shift) {
    BBox w; w.add(a); w.add(b);
    bool crossing = b.x < a.x;
    std::vector<uint32_t> ids;
    for (auto& e : doc.ents)
        if (crossing ? crosses_window(doc, e, w) : inside_window(doc, e, w)) ids.push_back(e.id);
    ids = expand_groups(ids);
    auto& target = (req.kind == ReqKind::Select) ? cmd_sel : sel;
    size_t before = target.size();
    for (auto x : ids) {
        auto it = std::find(target.begin(), target.end(), x);
        if (shift) { if (it != target.end()) target.erase(it); }
        else if (it == target.end()) target.push_back(x);
    }
    if (req.kind == ReqKind::Select)
        print(req.prompt + (crossing ? " Specify opposite corner: " : " Specify opposite corner: ") + std::to_string(target.size() - std::min(before, target.size())) + " found, " + std::to_string(target.size()) + " total");
}

void Cad::on_double_click(uint32_t id) {
    Entity* e = doc.find(id);
    if (!e) return;
    if (e->type == EType::Text || e->type == EType::MText) {
        edit_text_id = id;
        edit_text_buf = e->text;
        edit_text_height = e->height;
    } else if (e->type == EType::Dim) {
        edit_dim_id = id;
        edit_dim_value = dim_value(doc, *e);
        edit_dim_override = e->override_text;
    }
}

// ---------------------------------------------------------------------------
// Canvas mouse handling
// ---------------------------------------------------------------------------
void Cad::update_cursor(ImVec2 mouse, bool hovered) {
    mouse_pos = mouse;
    canvas_hovered = hovered;
    raw_cursor = to_world(mouse);
    ReqKind k = req.kind;
    bool point_mode = k == ReqKind::Point || k == ReqKind::Distance || k == ReqKind::Angle;
    snap = {};
    if (point_mode && hovered) compute_snap();
    if (snap.valid()) cursor = snap.p;
    else {
        cursor = raw_cursor;
        if (point_mode) apply_ortho_grid(cursor);
    }

    bool select_mode = !cmd || k == ReqKind::Select;
    hover_ent = (hovered && (select_mode || k == ReqKind::Pick) && !win_start) ? entity_at(raw_cursor) : 0;

    // Grips (idle only)
    hover_grip = false;
    if (!cmd && hovered && !sel.empty() && !win_start) {
        double best = 7.0;
        for (auto id : sel) {
            const Entity* e = doc.find(id);
            if (!e) continue;
            for (auto& [key, gp] : grips(doc, *e)) {
                ImVec2 s = to_screen(gp);
                double dd = std::hypot(s.x - mouse.x, s.y - mouse.y);
                if (dd < best) { best = dd; hover_grip = true; hover_grip_ent = id; hover_grip_key = key; hover_grip_pos = gp; }
            }
        }
        if (hover_grip) hover_ent = 0;
    }
}

void Cad::handle_canvas(bool hovered, bool clicked_left, bool clicked_right) {
    ImGuiIO& io = ImGui::GetIO();

    // Zoom (wheel) & pan (middle drag)
    if (hovered && io.MouseWheel != 0.0f) {
        Vec2 before = to_world(io.MousePos);
        zoom = std::clamp(zoom * std::pow(1.2, (double)io.MouseWheel), 1e-6, 1e7);
        view_user_set = true;
        Vec2 after = to_world(io.MousePos);
        view_center += before - after;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) && (hovered || io.MouseDownOwned[ImGuiMouseButton_Middle])) {
        view_center.x -= io.MouseDelta.x / zoom;
        view_center.y += io.MouseDelta.y / zoom;
        view_user_set = true;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) zoom_extents();

    ImVec2 mouse = io.MousePos;
    if (script_mouse) { hovered = true; mouse = to_screen(*script_mouse); }
    update_cursor(mouse, hovered);
    if (!hovered) return;

    if (clicked_right) {
        if (win_start) { win_start.reset(); return; }
        std::string t = input;
        input.clear();
        submit(t);
        return;
    }

    bool shift = io.KeyShift;
    // Window selection completion by drag-release
    if (win_start && win_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        ImVec2 a = to_screen(*win_start);
        if (std::hypot(a.x - mouse.x, a.y - mouse.y) > 5) {
            select_window(*win_start, raw_cursor, shift);
            win_start.reset();
        }
        win_dragging = false;
        return;
    }
    if (clicked_left) left_click(shift);
}

void Cad::left_click(bool shift) {
    switch (req.kind) {
    case ReqKind::None:
        if (cmd) break;
        if (hover_grip) {
            std::vector<std::pair<uint32_t, PKey>> gl;
            for (auto id : sel) {
                const Entity* e = doc.find(id);
                if (!e) continue;
                for (auto& [key, gp] : grips(doc, *e))
                    if (near(gp, hover_grip_pos, 1e-9 * std::max(1.0, len(gp)) + 1e-9)) gl.push_back({id, key});
            }
            cancel(true);
            cmd_checkpointed = false;
            cmd_name = "GRIP";
            cmd.emplace(grip_edit(*this, gl, hover_grip_pos));
            auto h = cmd->h;
            h.resume();
            if (cmd && cmd->h.done()) { cmd.reset(); cmd_name.clear(); req = {}; }
            break;
        }
        [[fallthrough]];
    case ReqKind::Select:
        if (win_start) {
            select_window(*win_start, raw_cursor, shift);
            win_start.reset();
        } else if (hover_ent) {
            select_click(raw_cursor, shift);
        } else {
            win_start = raw_cursor;
            win_dragging = true;
        }
        break;
    case ReqKind::Point: {
        Input in; in.st = Input::Ok; in.p = cursor; in.snap = snap; in.picked = true;
        resume(in);
        break;
    }
    case ReqKind::Distance:
        if (req.base) { Input in; in.st = Input::Ok; in.v = dist(*req.base, cursor); in.p = cursor; in.picked = true; resume(in); }
        break;
    case ReqKind::Angle:
        if (req.base && dist(*req.base, cursor) > EPS) {
            Input in; in.st = Input::Ok; in.v = angle_of(cursor - *req.base) * 180.0 / PI; in.p = cursor; in.picked = true; resume(in);
        }
        break;
    case ReqKind::Pick: {
        uint32_t id = entity_at(raw_cursor);
        if (!id) break;
        Input in; in.st = Input::Ok; in.ent = id; in.p = raw_cursor; in.shift = shift;
        resume(in);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Scripts (.scr): one input per line, like typing it and pressing Enter.
// Lines starting with ';' are comments; lines starting with '!' drive the mouse:
//   !click x,y   !shiftclick x,y   !dblclick x,y   !move x,y   !window x1,y1 x2,y2
//   !dimvalue x,y value [scale]   !esc   !wait n   !screenshot file.bmp   !exit
// ---------------------------------------------------------------------------
bool Cad::load_script(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    std::fclose(f);
    script.clear();
    script_pos = 0;
    size_t a = 0;
    while (a <= data.size()) {
        size_t b = data.find('\n', a);
        std::string line = data.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        script.push_back(line);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return true;
}

void Cad::script_step() {
    if (script_pos >= script.size()) return;
    if (script_wait > 0) { script_wait--; return; }
    std::string line = script[script_pos++];
    if (!line.empty() && line[0] == ';') return;
    if (line.empty() || line[0] != '!') { submit(line); return; }

    size_t sp = line.find(' ');
    std::string word = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    std::string rest = sp == std::string::npos ? std::string() : trim(line.substr(sp + 1));
    size_t sp2 = rest.find(' ');
    std::string first = rest.substr(0, sp2);
    std::string args = sp2 == std::string::npos ? std::string() : trim(rest.substr(sp2 + 1));
    Vec2 p;
    bool has_p = parse_point(first, last_point, p);
    if ((word == "click" || word == "shiftclick") && has_p) {
        script_mouse = p;
        update_cursor(to_screen(p), true);
        left_click(word == "shiftclick");
    } else if (word == "dblclick" && has_p) {
        if (uint32_t id = entity_at(p)) on_double_click(id);
    } else if (word == "move" && has_p) {
        script_mouse = p;
    } else if (word == "window") {
        Vec2 q;
        if (has_p && parse_point(args, last_point, q)) select_window(p, q, false);
    } else if (word == "dimvalue" && has_p) {
        double v = std::atof(args.c_str());
        bool scale = args.find("scale") != std::string::npos;
        uint32_t id = entity_at(p);
        const Entity* e = doc.find(id);
        if (e && e->type == EType::Dim) {
            force_checkpoint();
            print(std::string("Driving dimension to ") + format_number(v, 4) + (scale ? " (scale)" : " (stretch)"));
            drive_dim(doc, id, v, scale);
        } else print("!dimvalue: no dimension at point");
    } else if (word == "esc") {
        cancel();
    } else if (word == "wait") {
        script_wait = std::atoi(rest.c_str());
    } else if (word == "screenshot") {
        screenshot_request = rest;
    } else if (word == "exit") {
        quit_requested = true;
    } else if (word == "echo") {
        print(rest);
    } else {
        print("Unknown script directive: " + line);
    }
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
static void utf8_append(std::string& s, unsigned c) {
    if (c < 0x80) s += (char)c;
    else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
    else { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
}

void Cad::handle_keyboard() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    bool text_mode = req.kind == ReqKind::String;
    bool do_submit = false;
    for (ImWchar ch : io.InputQueueCharacters) {
        if (io.KeyCtrl) continue;
        if (ch == ' ' && !text_mode) { do_submit = true; break; }
        if (ch >= 32 && ch != 127) utf8_append(input, ch);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) do_submit = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !input.empty()) {
        while (!input.empty() && (input.back() & 0xC0) == 0x80) input.pop_back();
        if (!input.empty()) input.pop_back();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (cmd) cancel();
        else { sel.clear(); win_start.reset(); input.clear(); }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !history.empty()) {
        history_pos = history_pos < 0 ? (int)history.size() - 1 : std::max(0, history_pos - 1);
        input = history[history_pos];
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && history_pos >= 0) {
        history_pos++;
        if (history_pos >= (int)history.size()) { history_pos = -1; input.clear(); }
        else input = history[history_pos];
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !cmd && !input.empty()) {
        std::string u = upper(input);
        for (auto& d : command_table())
            if (std::string(d.name).rfind(u, 0) == 0) { input = d.name; break; }
    }
    auto toggle = [&](bool& flag, const char* name) {
        flag = !flag;
        print(std::string("<") + name + (flag ? " on>" : " off>"));
    };
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) toggle(osnap_on, "Osnap");
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) toggle(grid_on, "Grid");
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) toggle(ortho_on, "Ortho");
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) toggle(snap_on, "Snap");
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) toggle(dyn_on, "Dynamic Input");
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !cmd && !sel.empty()) {
        force_checkpoint();
        for (auto id : sel) doc.erase(id);
        print(std::to_string(sel.size()) + " object(s) erased");
        sel.clear();
    }
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { cancel(true); undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) { cancel(true); redo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !cmd) { sel.clear(); for (auto& e : doc.ents) sel.push_back(e.id); }
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) show_properties = !show_properties;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) toggle(grid_on, "Grid");
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) toggle(ortho_on, "Ortho");
    }

    if (do_submit) {
        std::string t = input;
        input.clear();
        submit(t);
    }
}

} // namespace cad
