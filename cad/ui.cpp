#include "cad.h"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <cstdio>
#include <map>
#include <set>

namespace cad {

// ---------------------------------------------------------------------------
// Theme (dark)
// ---------------------------------------------------------------------------
namespace ui_col {
static const ImVec4 ribbon{0.231f, 0.267f, 0.325f, 1};   // #3B4453
static const ImVec4 tabbar{0.169f, 0.192f, 0.231f, 1};   // #2B313B
static const ImVec4 panel_title{0.200f, 0.231f, 0.278f, 1};
static const ImVec4 status{0.169f, 0.200f, 0.243f, 1};
static const ImVec4 accent{0.024f, 0.588f, 0.843f, 1};   // #0696D7
} // namespace ui_col

void apply_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 2;
    s.FrameRounding = 2;
    s.GrabRounding = 2;
    s.PopupRounding = 2;
    s.ChildRounding = 0;
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 0;
    s.ItemSpacing = ImVec2(6, 4);
    s.FramePadding = ImVec2(6, 3);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.95f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.66f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.196f, 0.227f, 0.275f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.176f, 0.204f, 0.247f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.12f, 0.14f, 0.17f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.141f, 0.165f, 0.200f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.25f, 0.31f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.29f, 0.37f, 1);
    c[ImGuiCol_TitleBg] = ImVec4(0.141f, 0.165f, 0.200f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.169f, 0.204f, 0.255f, 1);
    c[ImGuiCol_Button] = ImVec4(0.26f, 0.30f, 0.36f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.37f, 0.46f, 1);
    c[ImGuiCol_ButtonActive] = ui_col::accent;
    c[ImGuiCol_Header] = ImVec4(0.26f, 0.31f, 0.38f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.37f, 0.46f, 1);
    c[ImGuiCol_HeaderActive] = ui_col::accent;
    c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.75f, 1.0f, 1);
    c[ImGuiCol_SliderGrab] = ui_col::accent;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.14f, 0.16f, 0.20f, 1);
    c[ImGuiCol_Separator] = ImVec4(0.14f, 0.16f, 0.20f, 1);
}

static std::string upper_s(std::string s) {
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

static void submit_keyword(Cad& c, const std::string& kw) {
    c.print(c.req.prompt + " " + kw);
    Input in;
    in.st = Input::Keyword;
    in.s = kw;
    c.resume(in);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void Cad::frame() {
    script_step();
    if (assoc_rev != doc.rev) {
        update_associative(doc);
        assoc_rev = doc.rev;
    }
    // Drop selections of entities that no longer exist
    std::erase_if(sel, [&](uint32_t id) { return !doc.find(id); });
    std::erase_if(cmd_sel, [&](uint32_t id) { return !doc.find(id); });

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    draw_ribbon(112);
    const float status_h = 28;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float prop_w = show_properties ? std::min(300.0f, avail.x * 0.3f) : 0.0f;
    draw_canvas(ImVec2(avail.x - prop_w, avail.y - status_h));
    if (show_properties) {
        ImGui::SameLine(0, 0);
        draw_properties(prop_w, avail.y - status_h);
    }
    draw_status_bar(status_h);
    ImGui::PopStyleVar(); // ItemSpacing
    ImGui::End();

    draw_popups();
    handle_keyboard();
}

// ---------------------------------------------------------------------------
// Ribbon
// ---------------------------------------------------------------------------
struct Tool { const char* icon; const char* label; const char* cmd; };

static bool ribbon_button(Cad& c, const Tool& t, float w, float h) {
    ImGui::PushID(t.label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##tool", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool active = c.cmd_name == t.cmd;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(40, 90, 140, 255), 3);
    else if (hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(75, 88, 108, 255), 3);
    float isz = 28;
    draw_icon(dl, t.icon, ImVec2(p.x + (w - isz) / 2, p.y + 5), isz, hovered || active);
    ImFont* f = ImGui::GetFont();
    float fs = 12.0f;
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, t.label);
    dl->AddText(f, fs, ImVec2(p.x + (w - ts.x) / 2, p.y + h - ts.y - 3), IM_COL32(220, 225, 232, 255), t.label);
    if (hovered) {
        const CmdDef* d = find_command(t.cmd);
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", t.label);
        if (d) {
            ImGui::TextDisabled("%s", d->desc);
            std::string al;
            for (auto a : d->aliases) { if (!al.empty()) al += ", "; al += a; }
            ImGui::Text("Command: %s%s%s", d->name, al.empty() ? "" : "   Alias: ", al.c_str());
        }
        ImGui::EndTooltip();
    }
    if (pressed) {
        if (std::string(t.cmd) == "U") { c.cancel(true); c.undo(); }
        else if (std::string(t.cmd) == "REDO") { c.cancel(true); c.redo(); }
        else if (std::string(t.cmd) == "ZOOMEXT") c.zoom_extents();
        else c.start_command(t.cmd);
    }
    ImGui::PopID();
    return pressed;
}

static void ribbon_panel(Cad& c, const char* title, const std::vector<Tool>& tools, float panel_h, float extra_w = 0, const std::function<void()>& extra = {}) {
    const float bw = 50, bh = 58, title_h = 18;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = tools.size() * bw + extra_w + 8;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p0.x, p0.y + panel_h - title_h), ImVec2(p0.x + w, p0.y + panel_h), ImGui::GetColorU32(ui_col::panel_title));
    ImFont* f = ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(12.0f, FLT_MAX, 0, title);
    dl->AddText(f, 12.0f, ImVec2(p0.x + (w - ts.x) / 2, p0.y + panel_h - title_h + 2), IM_COL32(190, 198, 210, 255), title);
    dl->AddLine(ImVec2(p0.x + w, p0.y + 4), ImVec2(p0.x + w, p0.y + panel_h - 2), IM_COL32(30, 35, 43, 255));
    float x = p0.x + 4;
    for (auto& t : tools) {
        ImGui::SetCursorScreenPos(ImVec2(x, p0.y + 6));
        ribbon_button(c, t, bw - 2, bh);
        x += bw;
    }
    if (extra) {
        ImGui::SetCursorScreenPos(ImVec2(x + 4, p0.y + 8));
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
        extra();
        ImGui::PopStyleVar();
        ImGui::EndGroup();
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x + w + 1, p0.y));
}

static const char* color_names[] = {"ByBlock", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White", "8", "9"};

static bool color_combo(const char* id, int& aci, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, "", ImGuiComboFlags_CustomPreview)) {
        for (int i = 1; i <= 9; i++) {
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##c", aci == i, 0, ImVec2(0, 16))) { aci = i; changed = true; }
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + 14, p.y + 14), aci_color(i));
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 20, p.y), ImGui::GetColorU32(ImGuiCol_Text), color_names[i]);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginComboPreview()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(p.x + 12, p.y + 13), aci_color(aci));
        ImGui::SetCursorScreenPos(ImVec2(p.x + 18, p.y));
        ImGui::TextUnformatted(color_names[std::clamp(aci, 0, 9)]);
        ImGui::EndComboPreview();
    }
    return changed;
}

void Cad::draw_ribbon(float height) {
    static int tab = 0;
    const float tab_h = 26;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + tab_h), ImGui::GetColorU32(ui_col::tabbar));
    dl->AddRectFilled(ImVec2(p0.x, p0.y + tab_h), ImVec2(p0.x + w, p0.y + height), ImGui::GetColorU32(ui_col::ribbon));

    // Application button
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##app", ImVec2(44, tab_h));
    dl->AddRectFilled(ImVec2(p0.x + 6, p0.y + 3), ImVec2(p0.x + 38, p0.y + tab_h - 3), IM_COL32(200, 40, 50, 255), 2);
    dl->AddText(ImGui::GetFont(), 15.0f, ImVec2(p0.x + 12, p0.y + 5), IM_COL32(255, 255, 255, 255), "SC");
    if (ImGui::IsItemClicked()) ImGui::OpenPopup("##appmenu");
    if (ImGui::BeginPopup("##appmenu")) {
        if (ImGui::MenuItem("New drawing")) { force_checkpoint(); cancel(true); doc.ents.clear(); doc.touch(); sel.clear(); }
        if (ImGui::MenuItem("Zoom extents")) zoom_extents();
        ImGui::Separator();
        ImGui::TextDisabled("StarCAD - a 2D drafting demo");
        ImGui::EndPopup();
    }

    const char* tabs[] = {"Home", "Annotate", "View"};
    float x = p0.x + 52;
    for (int i = 0; i < 3; i++) {
        ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
        ImVec2 a(x, p0.y + 3), b(x + ts.x + 24, p0.y + tab_h);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton(tabs[i], ImVec2(b.x - a.x, b.y - a.y))) tab = i;
        bool hov = ImGui::IsItemHovered();
        if (tab == i) dl->AddRectFilled(a, b, ImGui::GetColorU32(ui_col::ribbon), 3, ImDrawFlags_RoundCornersTop);
        else if (hov) dl->AddRectFilled(a, b, IM_COL32(60, 70, 85, 255), 3, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(a.x + 12, a.y + 4), tab == i ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 198, 210, 255), tabs[i]);
        x = b.x + 2;
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x + 2, p0.y + tab_h));
    float ph = height - tab_h;
    if (tab == 0) {
        ribbon_panel(*this, "Draw", {{"line", "Line", "LINE"}, {"pline", "Polyline", "PLINE"}, {"circle", "Circle", "CIRCLE"}, {"arc", "Arc", "ARC"},
                                     {"rect", "Rectangle", "RECTANG"}, {"polygon", "Polygon", "POLYGON"}, {"ellipse", "Ellipse", "ELLIPSE"}, {"hatch", "Hatch", "HATCH"}}, ph);
        ribbon_panel(*this, "Modify", {{"move", "Move", "MOVE"}, {"copy", "Copy", "COPY"}, {"rotate", "Rotate", "ROTATE"}, {"mirror", "Mirror", "MIRROR"},
                                       {"scale", "Scale", "SCALE"}, {"trim", "Trim", "TRIM"}, {"extend", "Extend", "EXTEND"}, {"join", "Join", "JOIN"},
                                       {"explode", "Explode", "EXPLODE"}, {"erase", "Erase", "ERASE"}}, ph);
        ribbon_panel(*this, "Annotation", {{"mtext", "Text", "MTEXT"}, {"dimlin", "Linear", "DIMLINEAR"}, {"dimrad", "Radius", "DIMRADIUS"}}, ph);
        ribbon_panel(*this, "Groups", {{"group", "Group", "GROUP"}, {"ungroup", "Ungroup", "UNGROUP"}}, ph, 96, [this] {
            ImGui::Checkbox("Group sel.", &group_sel);
            ImGui::SetItemTooltip("Selecting one member selects the whole group");
        });
        ribbon_panel(*this, "Properties", {{"props", "Properties", "PROPERTIES"}}, ph, 130, [this] {
            ImGui::TextUnformatted("Color");
            color_combo("##curcolor", cur_color, 120);
        });
        ribbon_panel(*this, "Utilities", {{"undo", "Undo", "U"}, {"redo", "Redo", "REDO"}}, ph);
    } else if (tab == 1) {
        ribbon_panel(*this, "Text", {{"mtext", "Multiline", "MTEXT"}, {"text", "Single Line", "TEXT"}}, ph, 120, [this] {
            ImGui::TextUnformatted("Height");
            ImGui::SetNextItemWidth(110);
            ImGui::InputDouble("##th", &text_height, 0, 0, "%.3f");
        });
        ribbon_panel(*this, "Dimensions", {{"dimlin", "Linear", "DIMLINEAR"}, {"dimali", "Aligned", "DIMALIGNED"}, {"dimrad", "Radius", "DIMRADIUS"}, {"dimdia", "Diameter", "DIMDIAMETER"}}, ph, 150, [this] {
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputDouble("DIMSCALE", &doc.dimstyle.scale, 0, 0, "%.3f")) { doc.dimstyle.scale = std::max(doc.dimstyle.scale, 1e-6); doc.touch(); }
            ImGui::SetNextItemWidth(80);
            if (ImGui::SliderInt("Precision", &doc.dimstyle.prec, 0, 6)) doc.touch();
        });
        ribbon_panel(*this, "Hatch", {{"hatch", "Hatch", "HATCH"}}, ph, 150, [this] {
            const char* pats[] = {"SOLID", "ANSI31", "ANSI37"};
            int p = (int)hatch_pat;
            ImGui::SetNextItemWidth(90);
            if (ImGui::Combo("Pattern", &p, pats, 3)) hatch_pat = (HatchPat)p;
            ImGui::SetNextItemWidth(90);
            ImGui::InputDouble("Scale", &hatch_scale, 0, 0, "%.3f");
            hatch_scale = std::max(hatch_scale, 1e-6);
        });
    } else {
        ribbon_panel(*this, "Navigate", {{"zoomext", "Extents", "ZOOMEXT"}}, ph);
        ribbon_panel(*this, "Palettes", {{"props", "Properties", "PROPERTIES"}}, ph);
        ribbon_panel(*this, "Drafting", {}, ph, 200, [this] {
            ImGui::SetNextItemWidth(90);
            ImGui::InputDouble("Grid spacing", &grid_step, 0, 0, "%.3f");
            ImGui::SetNextItemWidth(90);
            ImGui::InputDouble("Snap spacing", &snap_step, 0, 0, "%.3f");
            grid_step = std::max(grid_step, 1e-6);
            snap_step = std::max(snap_step, 1e-6);
        });
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + height));
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------
void Cad::draw_canvas(ImVec2 size) {
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    canvas_min = p0;
    canvas_max = ImVec2(p0.x + size.x, p0.y + size.y);
    // Fit the drawing until the user takes control of the view (the canvas may still be resizing at startup)
    static ImVec2 last_size(0, 0);
    if (!view_user_set && (last_size.x != size.x || last_size.y != size.y)) zoom_extents();
    last_size = size;

    ImGui::InvisibleButton("##canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    bool clicked_l = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool clicked_r = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (hovered && !cmd && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hover_ent) {
        on_double_click(hover_ent);
        clicked_l = false;
    }
    handle_canvas(hovered, clicked_l, clicked_r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    render_scene(dl);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    draw_command_line();
    if (dyn_on && canvas_hovered) draw_dynamic_input(ImGui::GetForegroundDrawList());
    // Re-submit the canvas rect as the last item so SameLine() places the palette beside it
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(size);
}

void Cad::draw_dynamic_input(ImDrawList* dl) {
    ImVec2 m = mouse_pos;
    std::string prompt;
    if (cmd && req.kind != ReqKind::None && req.kind != ReqKind::MText) {
        prompt = req.prompt;
        size_t lb = prompt.find(" or [");
        if (lb != std::string::npos) prompt = prompt.substr(0, lb);
        if (!prompt.empty() && prompt.back() == ':') prompt.pop_back();
        size_t nl = prompt.rfind('\n');
        if (nl != std::string::npos) prompt = prompt.substr(nl + 1);
    }
    std::string value = input;
    if (value.empty() && cmd) {
        bool point_mode = req.kind == ReqKind::Point || req.kind == ReqKind::Distance || req.kind == ReqKind::Angle;
        if (req.kind == ReqKind::Distance && req.base) value = format_number(dist(*req.base, cursor), 4);
        else if (req.kind == ReqKind::Angle && req.base) value = format_number(angle_of(cursor - *req.base) * 180 / PI, 2) + "\xC2\xB0";
        else if (point_mode && req.base) value = format_number(dist(*req.base, cursor), 4) + " < " + format_number(norm_angle(angle_of(cursor - *req.base)) * 180 / PI, 1) + "\xC2\xB0";
        else if (point_mode) value = format_number(cursor.x, 4) + ", " + format_number(cursor.y, 4);
    }
    if (prompt.empty() && value.empty()) return;
    ImVec2 p(m.x + 22, m.y + 22);
    ImVec2 ps = ImGui::CalcTextSize(prompt.c_str());
    ImVec2 vs = ImGui::CalcTextSize(value.empty() ? " " : value.c_str());
    float pad = 4;
    float x = p.x;
    if (!prompt.empty()) {
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + ps.x + pad * 2, p.y + ps.y + pad * 2), IM_COL32(52, 60, 72, 235), 2);
        dl->AddText(ImVec2(x + pad, p.y + pad), IM_COL32(230, 232, 236, 255), prompt.c_str());
        x += ps.x + pad * 2 + 4;
    }
    float vw = std::max(vs.x, 60.0f);
    bool typed = !input.empty();
    dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + vw + pad * 2, p.y + vs.y + pad * 2), typed ? IM_COL32(245, 245, 245, 255) : IM_COL32(70, 80, 95, 235), 2);
    dl->AddRect(ImVec2(x, p.y), ImVec2(x + vw + pad * 2, p.y + vs.y + pad * 2), IM_COL32(120, 140, 170, 255), 2);
    dl->AddText(ImVec2(x + pad, p.y + pad), typed ? IM_COL32(20, 20, 20, 255) : IM_COL32(220, 225, 232, 255), value.c_str());
}

// ---------------------------------------------------------------------------
// Command line (floating)
// ---------------------------------------------------------------------------
void Cad::draw_command_line() {
    static bool expanded = false;
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) expanded = !expanded;
    float cw = canvas_max.x - canvas_min.x;
    float w = std::min(980.0f, cw - 40.0f);
    float line_h = ImGui::GetTextLineHeight();
    float x = canvas_min.x + (cw - w) / 2;
    float y = canvas_max.y - 34 - 10;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // History above the command line
    int nhist = expanded ? 18 : 3;
    int count = std::min<int>(nhist, (int)log.size());
    if (count > 0) {
        float hh = count * (line_h + 2) + 8;
        ImVec2 a(x, y - hh - 2), b(x + w, y - 2);
        dl->AddRectFilled(a, b, IM_COL32(28, 33, 40, expanded ? 235 : 150), 3);
        for (int i = 0; i < count; i++) {
            const std::string& s = log[log.size() - count + i];
            float alpha = expanded ? 1.0f : 0.55f + 0.45f * (float)(i + 1) / count;
            dl->AddText(ImVec2(a.x + 10, a.y + 4 + i * (line_h + 2)), IM_COL32(220, 225, 232, (int)(255 * alpha)), s.c_str());
        }
    }

    // Suggestions (AutoComplete)
    if (!cmd && !input.empty()) {
        std::string u = upper_s(input);
        std::vector<const CmdDef*> matches;
        for (auto& d : command_table()) {
            bool m = std::string(d.name).rfind(u, 0) == 0;
            for (auto al : d.aliases) m = m || std::string(al) == u;
            if (m) matches.push_back(&d);
        }
        if (!matches.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(x, y - 4 - std::min<int>(8, (int)matches.size()) * (line_h + 6) - (count > 0 ? count * (line_h + 2) + 10 : 0)));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.19f, 0.23f, 0.97f));
            ImGui::BeginChild("##suggest", ImVec2(260, std::min<int>(8, (int)matches.size()) * (line_h + 6) + 4), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            for (size_t i = 0; i < matches.size() && i < 8; i++) {
                std::string lbl = matches[i]->name;
                if (!matches[i]->aliases.empty()) lbl += std::string("  (") + matches[i]->aliases[0] + ")";
                if (ImGui::Selectable(lbl.c_str(), i == 0)) { input.clear(); start_command(matches[i]->name); }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    // Input line
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.93f, 0.94f, 0.95f, 0.96f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    std::string clicked_kw;
    ImGui::BeginChild("##cmdline", ImVec2(w, 34), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.12f, 0.13f, 0.15f, 1));
    ImGui::TextColored(ImVec4(0.35f, 0.38f, 0.42f, 1), ">_");
    ImGui::SameLine(0, 8);
    if (cmd && req.kind != ReqKind::None) {
        const std::string& pr = req.prompt;
        std::string display = pr;
        size_t nl = display.rfind('\n');
        if (nl != std::string::npos) display = display.substr(nl + 1);
        size_t a = display.find('['), b = display.find(']');
        std::string cname = cmd_name == "GRIP" ? "" : cmd_name + " ";
        if (!cname.empty()) { ImGui::TextColored(ImVec4(0.1f, 0.35f, 0.65f, 1), "%s", cname.c_str()); ImGui::SameLine(0, 4); }
        if (a != std::string::npos && b != std::string::npos && b > a) {
            ImGui::TextUnformatted(display.substr(0, a + 1).c_str());
            auto kws = prompt_keywords(display);
            for (size_t i = 0; i < kws.size(); i++) {
                ImGui::SameLine(0, 0);
                ImVec2 tp = ImGui::GetCursorScreenPos();
                ImVec2 ts = ImGui::CalcTextSize(kws[i].c_str());
                ImGui::PushID((int)i);
                if (ImGui::InvisibleButton("##kw", ts)) clicked_kw = kws[i];
                bool hov = ImGui::IsItemHovered();
                ImGui::PopID();
                ImGui::GetWindowDrawList()->AddRectFilled(tp, ImVec2(tp.x + ts.x, tp.y + ts.y), hov ? IM_COL32(170, 205, 245, 255) : IM_COL32(215, 228, 245, 255), 2);
                ImGui::GetWindowDrawList()->AddText(tp, IM_COL32(20, 70, 150, 255), kws[i].c_str());
                if (i + 1 < kws.size()) { ImGui::SameLine(0, 0); ImGui::TextUnformatted("/"); }
            }
            ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(display.substr(b).c_str());
        } else {
            ImGui::TextUnformatted(display.c_str());
        }
        ImGui::SameLine(0, 6);
    }
    std::string shown = input;
    bool caret = std::fmod(ImGui::GetTime(), 1.0) < 0.55;
    if (!cmd && input.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.52f, 1), "Type a command");
        ImGui::SameLine(0, 0);
    } else {
        ImGui::TextUnformatted(shown.c_str());
        ImGui::SameLine(0, 0);
    }
    if (caret) {
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(cp.x + 1, cp.y), ImVec2(cp.x + 1, cp.y + line_h), IM_COL32(20, 20, 20, 255), 1.5f);
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!clicked_kw.empty()) submit_keyword(*this, clicked_kw);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
static bool status_toggle(const char* label, bool on, const char* tip) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.12f, 0.33f, 0.52f, 1) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImVec4(0.16f, 0.42f, 0.64f, 1) : ImVec4(0.28f, 0.33f, 0.40f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.55f, 0.82f, 1.0f, 1) : ImVec4(0.62f, 0.66f, 0.72f, 1));
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("%s", tip);
    return pressed;
}

void Cad::draw_status_bar(float h) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ui_col::status));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 6, p0.y + 2));

    // Model tab
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.30f, 0.36f, 1));
    ImGui::Button("Model");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 16);

    char buf[96];
    std::snprintf(buf, sizeof buf, "%.4f, %.4f, 0.0000", cursor.x, cursor.y);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.75f, 0.79f, 0.85f, 1), "%s", buf);
    ImGui::SameLine(0, 18);

    if (status_toggle("MODEL", true, "Model space")) {}
    ImGui::SameLine();
    if (status_toggle("GRID", grid_on, "Display drawing grid (F7)")) grid_on = !grid_on;
    ImGui::SameLine();
    if (status_toggle("SNAP", snap_on, "Snap mode (F9)")) snap_on = !snap_on;
    ImGui::SameLine();
    if (status_toggle("ORTHO", ortho_on, "Ortho mode (F8)")) ortho_on = !ortho_on;
    ImGui::SameLine();
    if (status_toggle("OSNAP", osnap_on, "Object snap (F3)")) osnap_on = !osnap_on;
    ImGui::SameLine(0, 0);
    if (status_toggle("v##os", false, "Object snap settings")) ImGui::OpenPopup("##osnapmenu");
    if (ImGui::BeginPopup("##osnapmenu")) {
        struct M { uint32_t bit; const char* name; } modes[] = {{OS_END, "Endpoint"}, {OS_MID, "Midpoint"}, {OS_CEN, "Center"}, {OS_QUA, "Quadrant"},
                                                              {OS_INT, "Intersection"}, {OS_PER, "Perpendicular"}, {OS_INS, "Insertion"}, {OS_NEA, "Nearest"}};
        for (auto& m : modes) {
            bool on = osnap_modes & m.bit;
            if (ImGui::MenuItem(m.name, nullptr, on)) osnap_modes ^= m.bit;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Object Snap Settings...")) show_osnap_settings = true;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (status_toggle("DYN", dyn_on, "Dynamic input (F12)")) dyn_on = !dyn_on;
    ImGui::SameLine();
    if (status_toggle("GROUP", group_sel, "Group selection")) group_sel = !group_sel;

    float right = p0.x + w - 120;
    ImGui::SameLine();
    if (ImGui::GetCursorScreenPos().x < right) ImGui::SetCursorScreenPos(ImVec2(right, p0.y + 2));
    if (status_toggle("Properties", show_properties, "Properties palette (Ctrl+1)")) show_properties = !show_properties;
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h));
    ImGui::Dummy(ImVec2(0, 0));
}

// ---------------------------------------------------------------------------
// Properties palette
// ---------------------------------------------------------------------------
static bool g_prop_editing = false;

static void prop_label(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

static void begin_edit(Cad& c) {
    if (!g_prop_editing) { c.force_checkpoint(); g_prop_editing = true; }
}
static void end_edit_check() { if (ImGui::IsItemDeactivated()) g_prop_editing = false; }

static bool prop_double(Cad& c, const char* label, double& v, double scale = 1.0, const char* fmt = "%.4f") {
    prop_label(label);
    double t = v * scale;
    ImGui::PushID(label);
    bool ch = ImGui::InputDouble("##v", &t, 0, 0, fmt);
    ImGui::PopID();
    if (ch) { begin_edit(c); v = t / scale; c.doc.touch(); }
    end_edit_check();
    return ch;
}

static void prop_ro(const char* label, const std::string& v) {
    prop_label(label);
    ImGui::TextDisabled("%s", v.c_str());
}

static bool prop_vec(Cad& c, const char* label, Vec2& v) {
    bool a = prop_double(c, (std::string(label) + " X").c_str(), v.x);
    bool b = prop_double(c, (std::string(label) + " Y").c_str(), v.y);
    return a || b;
}

void Cad::draw_properties(float width, float height) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), IM_COL32(44, 51, 62, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    ImGui::BeginChild("##props", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.92f, 1), "PROPERTIES");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
    if (ImGui::SmallButton("x")) show_properties = false;
    ImGui::Separator();

    const auto& s = !sel.empty() ? sel : cmd_sel;
    ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

    if (s.empty()) {
        ImGui::TextDisabled("No selection");
        if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##gen", 2, tf)) {
            prop_label("Color");
            color_combo("##cc", cur_color, -FLT_MIN);
            prop_label("Grid spacing");
            ImGui::InputDouble("##gs", &grid_step, 0, 0, "%.4f");
            grid_step = std::max(grid_step, 1e-6);
            prop_label("Snap spacing");
            ImGui::InputDouble("##ss", &snap_step, 0, 0, "%.4f");
            snap_step = std::max(snap_step, 1e-6);
            prop_label("Text height");
            ImGui::InputDouble("##th", &text_height, 0, 0, "%.4f");
            ImGui::EndTable();
        }
        if (ImGui::CollapsingHeader("Dimension style (ISO-25)", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##ds", 2, tf)) {
            DimStyle& d = doc.dimstyle;
            if (prop_double(*this, "DIMSCALE", d.scale)) d.scale = std::max(d.scale, 1e-6);
            prop_double(*this, "Text height", d.txt);
            prop_double(*this, "Arrow size", d.asz);
            prop_double(*this, "Ext. offset", d.exo);
            prop_double(*this, "Ext. beyond", d.exe);
            prop_double(*this, "Text gap", d.gap);
            prop_label("Precision");
            if (ImGui::SliderInt("##prec", &d.prec, 0, 6)) doc.touch();
            ImGui::EndTable();
        }
        if (ImGui::CollapsingHeader("Hatch", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##hs", 2, tf)) {
            const char* pats[] = {"SOLID", "ANSI31", "ANSI37"};
            int p = (int)hatch_pat;
            prop_label("Pattern");
            if (ImGui::Combo("##hp", &p, pats, 3)) hatch_pat = (HatchPat)p;
            prop_label("Scale");
            ImGui::InputDouble("##hsc", &hatch_scale, 0, 0, "%.4f");
            hatch_scale = std::max(hatch_scale, 1e-6);
            prop_label("Angle");
            ImGui::InputDouble("##ha", &hatch_angle, 0, 0, "%.2f");
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Tip: double-click a dimension to\nchange its value and drive the geometry.");
    } else if (s.size() > 1) {
        std::map<std::string, int> counts;
        for (auto id : s) if (auto* e = doc.find(id)) counts[type_name(*e)]++;
        ImGui::Text("All (%d)", (int)s.size());
        for (auto& [n, k] : counts) ImGui::BulletText("%s (%d)", n.c_str(), k);
        if (ImGui::BeginTable("##multi", 2, tf)) {
            int col = -1;
            for (auto id : s) if (auto* e = doc.find(id)) { if (col == -1) col = e->color; else if (col != e->color) col = 0; }
            prop_label("Color");
            if (color_combo("##mc", col, -FLT_MIN)) { force_checkpoint(); for (auto id : s) if (auto* e = doc.find(id)) e->color = col; }
            ImGui::EndTable();
        }
    } else {
        Entity* e = doc.find(s[0]);
        if (e) {
            ImGui::TextColored(ImVec4(0.55f, 0.82f, 1.0f, 1), "%s", type_name(*e));
            if (ImGui::BeginTable("##one", 2, tf)) {
                prop_label("Color");
                int col = e->color;
                if (color_combo("##ec", col, -FLT_MIN)) { force_checkpoint(); e->color = col; }
                prop_ro("Group", e->group ? "*A" + std::to_string(e->group) : "None");
                switch (e->type) {
                case EType::Line: {
                    prop_vec(*this, "Start", e->v[0].p);
                    prop_vec(*this, "End", e->v[1].p);
                    Vec2 d = e->v[1].p - e->v[0].p;
                    prop_ro("Length", format_number(len(d), 4));
                    prop_ro("Angle", format_number(norm_angle(angle_of(d)) * 180 / PI, 4));
                    break;
                }
                case EType::Circle: {
                    prop_vec(*this, "Center", e->c);
                    if (prop_double(*this, "Radius", e->r)) e->r = std::max(e->r, 1e-9);
                    if (prop_double(*this, "Diameter", e->r, 2.0)) e->r = std::max(e->r, 1e-9);
                    prop_ro("Circumference", format_number(TAU * e->r, 4));
                    prop_ro("Area", format_number(PI * e->r * e->r, 4));
                    break;
                }
                case EType::Arc: {
                    prop_vec(*this, "Center", e->c);
                    if (prop_double(*this, "Radius", e->r)) e->r = std::max(e->r, 1e-9);
                    prop_double(*this, "Start angle", e->a0, 180 / PI, "%.2f");
                    prop_double(*this, "End angle", e->a1, 180 / PI, "%.2f");
                    prop_ro("Arc length", format_number(entity_segs(*e)[0].length(), 4));
                    break;
                }
                case EType::Polyline: {
                    static int vi = 0;
                    vi = std::clamp(vi, 0, (int)e->v.size() - 1);
                    prop_label("Closed");
                    bool cl = e->closed;
                    if (ImGui::Checkbox("##closed", &cl)) { force_checkpoint(); e->closed = cl; doc.touch(); }
                    prop_label("Vertex");
                    ImGui::SliderInt("##vi", &vi, 0, (int)e->v.size() - 1);
                    prop_vec(*this, "Vertex", e->v[vi].p);
                    prop_double(*this, "Bulge", e->v[vi].bulge);
                    double L = 0; for (auto& sg : entity_segs(*e)) L += sg.length();
                    prop_ro("Length", format_number(L, 4));
                    if (e->closed) prop_ro("Area", format_number(std::fabs(polygon_area(closed_loop(*e))), 4));
                    break;
                }
                case EType::Ellipse: {
                    prop_vec(*this, "Center", e->c);
                    double maj = len(e->major), mi = maj * e->ratio, ang = angle_of(e->major);
                    prop_label("Major radius");
                    double t = maj;
                    if (ImGui::InputDouble("##maj", &t, 0, 0, "%.4f") && t > 0) { begin_edit(*this); e->major = dir(ang) * t; e->ratio = std::min(1.0, mi / t); doc.touch(); }
                    end_edit_check();
                    prop_label("Minor radius");
                    t = mi;
                    if (ImGui::InputDouble("##min", &t, 0, 0, "%.4f") && t > 0 && t <= maj) { begin_edit(*this); e->ratio = t / maj; doc.touch(); }
                    end_edit_check();
                    prop_ro("Rotation", format_number(norm_angle(ang) * 180 / PI, 2));
                    break;
                }
                case EType::Text:
                case EType::MText: {
                    prop_label("Contents");
                    bool ch = e->type == EType::Text ? ImGui::InputText("##txt", &e->text) : ImGui::InputTextMultiline("##txt", &e->text, ImVec2(-FLT_MIN, 80));
                    if (ImGui::IsItemActivated()) begin_edit(*this);
                    if (ch) doc.touch();
                    end_edit_check();
                    if (prop_double(*this, "Height", e->height)) e->height = std::max(e->height, 1e-6);
                    prop_double(*this, "Rotation", e->rot, 180 / PI, "%.2f");
                    if (e->type == EType::MText) prop_double(*this, "Width", e->width);
                    prop_vec(*this, "Position", e->v[0].p);
                    break;
                }
                case EType::Hatch: {
                    const char* pats[] = {"SOLID", "ANSI31", "ANSI37"};
                    int p = (int)e->pat;
                    prop_label("Pattern");
                    if (ImGui::Combo("##hp", &p, pats, 3)) { force_checkpoint(); e->pat = (HatchPat)p; }
                    if (prop_double(*this, "Scale", e->pscale)) e->pscale = std::max(e->pscale, 1e-6);
                    prop_double(*this, "Angle", e->pangle, 180 / PI, "%.2f");
                    prop_ro("Associative", (!e->bnd.empty() || e->has_seed) ? "Yes" : "No");
                    double area = 0;
                    for (size_t i = 0; i < e->loops.size(); i++) area += (i == 0 ? 1 : -1) * std::fabs(polygon_area(e->loops[i]));
                    prop_ro("Area", format_number(std::fabs(area), 4));
                    break;
                }
                case EType::Dim: {
                    double val = dim_value(doc, *e);
                    static int mode = 0;
                    prop_label("Measurement");
                    double t = val;
                    ImGui::InputDouble("##meas", &t, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
                    if (ImGui::IsItemDeactivatedAfterEdit() && t > 0 && std::fabs(t - val) > 1e-12) {
                        force_checkpoint();
                        if (!drive_dim(doc, e->id, t, mode == 1)) print("Cannot drive this dimension.");
                        e = doc.find(s[0]);
                    }
                    ImGui::SetItemTooltip("Type a new value and press Enter: the referenced geometry is changed to match.");
                    prop_label("Drive mode");
                    ImGui::RadioButton("Stretch", &mode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("Scale", &mode, 1);
                    if (e) {
                        prop_label("Text override");
                        if (ImGui::InputText("##ovr", &e->override_text)) doc.touch();
                        if (ImGui::IsItemActivated()) begin_edit(*this);
                        end_edit_check();
                        bool assoc = e->r1.valid() || e->r2.valid() || e->rent;
                        prop_ro("Associative", assoc ? "Yes" : "No");
                    }
                    break;
                }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------
// Popups
// ---------------------------------------------------------------------------
void Cad::draw_popups() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f);

    // MText editor (in-command)
    if (req.kind == ReqKind::MText) {
        static bool focus = true;
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_Appearing);
        ImGui::Begin("Text Editor", nullptr, ImGuiWindowFlags_NoCollapse);
        ImGui::SetNextItemWidth(120);
        ImGui::InputDouble("Text height", &mtext_height, 0, 0, "%.3f");
        mtext_height = std::max(mtext_height, 1e-6);
        if (focus) { ImGui::SetKeyboardFocusHere(); focus = false; }
        ImGui::InputTextMultiline("##mt", &mtext_buf, ImVec2(-FLT_MIN, -40));
        bool ok = ImGui::Button("OK", ImVec2(90, 0));
        ImGui::SameLine();
        bool cancel_b = ImGui::Button("Cancel", ImVec2(90, 0));
        ImGui::SameLine();
        ImGui::TextDisabled("Enter = new line");
        ImGui::End();
        if (ok) { focus = true; Input in; in.st = Input::Ok; in.s = mtext_buf; resume(in); }
        else if (cancel_b) { focus = true; Input in; in.st = Input::None; resume(in); }
    }

    // Text edit (double-click)
    if (edit_text_id) {
        Entity* e = doc.find(edit_text_id);
        if (!e) edit_text_id = 0;
        else {
            static uint32_t focused_for = 0;
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::Begin("Edit Text", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
            if (focused_for != edit_text_id) { ImGui::SetKeyboardFocusHere(); focused_for = edit_text_id; }
            bool enter = false;
            if (e->type == EType::Text) enter = ImGui::InputText("##t", &edit_text_buf, ImGuiInputTextFlags_EnterReturnsTrue);
            else ImGui::InputTextMultiline("##t", &edit_text_buf, ImVec2(420, 160));
            ImGui::SetNextItemWidth(120);
            ImGui::InputDouble("Height", &edit_text_height, 0, 0, "%.3f");
            bool ok = ImGui::Button("OK", ImVec2(90, 0)) || enter;
            ImGui::SameLine();
            bool cn = ImGui::Button("Cancel", ImVec2(90, 0));
            ImGui::End();
            if (ok) {
                force_checkpoint();
                e->text = edit_text_buf;
                if (edit_text_height > 0) e->height = edit_text_height;
                doc.touch();
                edit_text_id = 0; focused_for = 0;
            } else if (cn) { edit_text_id = 0; focused_for = 0; }
        }
    }

    // Dimension edit (double-click): changing the value drives the referenced geometry
    if (edit_dim_id) {
        Entity* e = doc.find(edit_dim_id);
        if (!e) edit_dim_id = 0;
        else {
            static uint32_t focused_for = 0;
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::Begin("Edit Dimension", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextDisabled("%s  -  current value %s", type_name(*e), format_number(dim_value(doc, *e), 4).c_str());
            if (focused_for != edit_dim_id) { ImGui::SetKeyboardFocusHere(); focused_for = edit_dim_id; }
            ImGui::SetNextItemWidth(160);
            bool enter = ImGui::InputDouble("Value", &edit_dim_value, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::RadioButton("Stretch geometry", &edit_dim_mode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Scale geometry", &edit_dim_mode, 1);
            ImGui::TextDisabled(edit_dim_mode == 0 ? "Moves the geometry at the second point (keeps the first point fixed)."
                                                   : "Scales the referenced objects about the first point.");
            ImGui::SetNextItemWidth(160);
            ImGui::InputText("Text override", &edit_dim_override);
            ImGui::SameLine();
            ImGui::TextDisabled("(<> = value)");
            bool ok = ImGui::Button("OK", ImVec2(90, 0)) || enter;
            ImGui::SameLine();
            bool cn = ImGui::Button("Cancel", ImVec2(90, 0));
            ImGui::End();
            if (ok) {
                force_checkpoint();
                double cur = dim_value(doc, *e);
                e->override_text = edit_dim_override;
                if (edit_dim_value > 0 && std::fabs(edit_dim_value - cur) > 1e-12) {
                    bool assoc = e->r1.valid() || e->r2.valid() || e->rent;
                    if (!drive_dim(doc, edit_dim_id, edit_dim_value, edit_dim_mode == 1)) print("Cannot drive this dimension.");
                    else if (!assoc) print("Dimension is not associated with geometry; only the dimension was changed.");
                }
                doc.touch();
                edit_dim_id = 0; focused_for = 0;
            } else if (cn) { edit_dim_id = 0; focused_for = 0; }
        }
    }

    // Drafting settings
    if (show_osnap_settings) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::Begin("Drafting Settings", &show_osnap_settings, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Checkbox("Object Snap On (F3)", &osnap_on);
        ImGui::Separator();
        struct M { uint32_t bit; const char* name; } modes[] = {{OS_END, "Endpoint"}, {OS_MID, "Midpoint"}, {OS_CEN, "Center"}, {OS_QUA, "Quadrant"},
                                                              {OS_INT, "Intersection"}, {OS_PER, "Perpendicular"}, {OS_INS, "Insertion"}, {OS_NEA, "Nearest"}};
        int i = 0;
        for (auto& m : modes) {
            bool on = osnap_modes & m.bit;
            if (ImGui::Checkbox(m.name, &on)) osnap_modes ^= m.bit;
            if (++i % 2) ImGui::SameLine(180);
        }
        if (ImGui::Button("Select All")) osnap_modes = 0xFF;
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) osnap_modes = 0;
        ImGui::Separator();
        ImGui::Checkbox("Snap On (F9)", &snap_on);
        ImGui::SameLine(180);
        ImGui::SetNextItemWidth(100);
        ImGui::InputDouble("Snap spacing", &snap_step, 0, 0, "%.4f");
        ImGui::Checkbox("Grid On (F7)", &grid_on);
        ImGui::SameLine(180);
        ImGui::SetNextItemWidth(100);
        ImGui::InputDouble("Grid spacing", &grid_step, 0, 0, "%.4f");
        snap_step = std::max(snap_step, 1e-6);
        grid_step = std::max(grid_step, 1e-6);
        ImGui::Checkbox("Ortho (F8)", &ortho_on);
        if (ImGui::Button("OK", ImVec2(90, 0))) show_osnap_settings = false;
        ImGui::End();
    }
}

} // namespace cad
