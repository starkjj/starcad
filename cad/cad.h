#pragma once

#include "entity.h"

#include <imgui.h>

#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cad {

// ---------------------------------------------------------------------------
// Coroutine task. Commands are written as straight-line coroutines that
// `co_await` user input, mirroring a classic CAD prompt sequence.
// ---------------------------------------------------------------------------
template <class T>
struct TaskPromiseBase {
    std::coroutine_handle<> cont;
    std::suspend_always initial_suspend() noexcept { return {}; }
    struct Final {
        bool await_ready() noexcept { return false; }
        template <class P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
            auto c = h.promise().cont;
            return c ? c : std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };
    Final final_suspend() noexcept { return {}; }
    void unhandled_exception() { std::terminate(); }
};

template <class T = void>
struct Task {
    struct promise_type : TaskPromiseBase<T> {
        std::optional<T> value;
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        void return_value(T v) { value = std::move(v); }
    };
    std::coroutine_handle<promise_type> h;
    explicit Task(std::coroutine_handle<promise_type> h_) : h(h_) {}
    Task(Task&& o) noexcept : h(std::exchange(o.h, {})) {}
    Task& operator=(Task&&) = delete;
    ~Task() { if (h) h.destroy(); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept { h.promise().cont = c; return h; }
    T await_resume() { return std::move(*h.promise().value); }
};

template <>
struct Task<void> {
    struct promise_type : TaskPromiseBase<void> {
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        void return_void() {}
    };
    std::coroutine_handle<promise_type> h;
    explicit Task(std::coroutine_handle<promise_type> h_) : h(h_) {}
    Task(Task&& o) noexcept : h(std::exchange(o.h, {})) {}
    Task& operator=(Task&&) = delete;
    ~Task() { if (h) h.destroy(); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept { h.promise().cont = c; return h; }
    void await_resume() {}
};

// ---------------------------------------------------------------------------
// Input requests
// ---------------------------------------------------------------------------
enum class ReqKind { None, Point, Distance, Angle, Integer, Real, Keyword, String, Select, Pick, MText };

using PreviewFn = std::function<std::vector<Entity>(Vec2)>;

struct Req {
    ReqKind kind = ReqKind::None;
    std::string prompt;          // e.g. "Specify next point or [Close/Undo]:"
    std::optional<Vec2> base;    // rubber-band origin, relative input and ortho base
    bool rubber = true;          // draw a rubber-band line from base
    bool allow_none = true;      // Enter on empty input is accepted (returns Input::None)
    bool snap = true;            // object snaps active for this request
    PreviewFn preview;           // ghost entities that follow the cursor
    std::string text_init;       // MText editor initial content
};

enum Osnap : uint32_t {
    OS_END = 1, OS_MID = 2, OS_CEN = 4, OS_QUA = 8, OS_INT = 16, OS_PER = 32, OS_NEA = 64, OS_INS = 128,
};

struct SnapInfo {
    uint32_t mode = 0;
    Vec2 p;
    uint32_t ent = 0;
    PKey key;
    bool valid() const { return mode != 0; }
    PointRef ref() const { return (ent && key.kind != KeyKind::None) ? PointRef{ent, key} : PointRef{}; }
};

struct Input {
    enum St { Ok, None, Keyword } st = None;
    Vec2 p;
    double v = 0;
    std::string s;       // text or keyword
    uint32_t ent = 0;    // picked entity
    bool shift = false;  // shift held on pick (trim -> extend)
    bool picked = false; // value came from a mouse click (not typed)
    SnapInfo snap;       // osnap used for a picked point
    std::vector<uint32_t> sel;
    bool ok() const { return st == Ok; }
    bool kw(const char* k) const { return st == Keyword && s == k; }
};

class Cad;
struct InputAwait {
    Cad* c;
    Req r;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h);
    Input await_resume();
};

struct CmdDef {
    const char* name;
    std::vector<const char*> aliases;
    Task<> (*fn)(Cad&);
    const char* desc;
};
const std::vector<CmdDef>& command_table();
const CmdDef* find_command(const std::string& name);

struct Snapshot {
    std::vector<Entity> ents;
    uint32_t next_id = 1, next_group = 1;
    DimStyle dimstyle;
};

// ---------------------------------------------------------------------------
// The application
// ---------------------------------------------------------------------------
class Cad {
public:
    Cad();
    void frame(); // builds the whole UI for one frame

    // Document & selection
    Doc doc;
    std::vector<uint32_t> sel;      // gripped selection (noun/verb)
    std::vector<uint32_t> pre_sel;  // selection handed to the running command
    std::vector<uint32_t> cmd_sel;  // selection being built by "Select objects:"
    bool is_selected(uint32_t id) const;
    std::vector<uint32_t> expand_groups(const std::vector<uint32_t>& ids) const;

    // View
    Vec2 view_center{210, 148.5};
    double zoom = 2.5; // pixels per drawing unit
    bool view_user_set = false; // set once the user pans/zooms
    ImVec2 canvas_min{}, canvas_max{};
    ImVec2 to_screen(Vec2 p) const;
    Vec2 to_world(ImVec2 s) const;
    void zoom_extents();
    void zoom_window(Vec2 a, Vec2 b);

    // Drafting settings
    bool grid_on = true, snap_on = false, ortho_on = false, osnap_on = true, dyn_on = true, group_sel = true;
    double grid_step = 10, snap_step = 10;
    uint32_t osnap_modes = OS_END | OS_MID | OS_CEN | OS_QUA | OS_INT | OS_PER | OS_INS;
    uint32_t osnap_override = 0;
    int cur_color = 7;
    double text_height = 2.5;
    HatchPat hatch_pat = HatchPat::ANSI31;
    double hatch_scale = 1.0, hatch_angle = 0.0;
    int polygon_sides = 4;
    double last_radius = 10;
    bool show_properties = true;
    Entity styled(Entity e) const { e.color = cur_color; return e; }

    // Command machinery
    std::optional<Task<>> cmd;
    std::string cmd_name, last_cmd;
    Req req;
    std::coroutine_handle<> waiting;
    Input result;
    Vec2 last_point{};
    bool cmd_checkpointed = false;
    void start_command(const std::string& name);
    void cancel(bool quiet = false);
    void resume(Input in);
    void print(const std::string& s);

    // Awaitable helpers used by command coroutines
    InputAwait ask(Req r) { return InputAwait{this, std::move(r)}; }
    InputAwait get_point(std::string prompt, std::optional<Vec2> base = {}, PreviewFn preview = {}, bool rubber = true);
    InputAwait get_distance(std::string prompt, std::optional<Vec2> base = {}, PreviewFn preview = {});
    InputAwait get_angle(std::string prompt, std::optional<Vec2> base = {}, PreviewFn preview = {});
    InputAwait get_integer(std::string prompt);
    InputAwait get_real(std::string prompt);
    InputAwait get_keyword(std::string prompt);
    InputAwait get_string(std::string prompt, PreviewFn preview = {});
    InputAwait get_pick(std::string prompt);
    Task<std::vector<uint32_t>> select_objects(std::string prompt = "Select objects:");

    // Undo
    std::vector<Snapshot> undo_stack, redo_stack;
    Snapshot snapshot() const;
    void restore(const Snapshot& s);
    void checkpoint();         // once per command: saves state for UNDO
    void force_checkpoint();   // unconditional
    void undo();
    void redo();

    // Text entry (command line)
    std::string input;
    std::vector<std::string> log;
    std::vector<std::string> history;
    int history_pos = -1;
    void submit(const std::string& text);

    // Frame state
    Vec2 cursor{};          // world cursor (after snap/ortho)
    Vec2 raw_cursor{};      // world cursor (raw)
    bool canvas_hovered = false;
    SnapInfo snap;
    uint32_t hover_ent = 0;
    bool hover_grip = false;
    uint32_t hover_grip_ent = 0;
    PKey hover_grip_key;
    Vec2 hover_grip_pos;
    std::optional<Vec2> win_start;   // selection window first corner (world)
    bool win_dragging = false;
    uint64_t assoc_rev = ~0ull;
    double pick_tol() const { return 5.0 / zoom; }

    // Popups
    uint32_t edit_text_id = 0;
    std::string edit_text_buf;
    double edit_text_height = 0;
    uint32_t edit_dim_id = 0;
    double edit_dim_value = 0;
    int edit_dim_mode = 0; // 0 = stretch, 1 = scale
    std::string edit_dim_override;
    bool show_osnap_settings = false;
    std::string mtext_buf;
    double mtext_height = 2.5;

    // Implementation (cad.cpp / ui.cpp / render.cpp)
    void handle_canvas(bool hovered, bool clicked_left, bool clicked_right);
    void update_cursor(ImVec2 mouse, bool hovered);
    void left_click(bool shift);
    ImVec2 mouse_pos{};

    // Scripting (.scr) - also used for automated testing
    std::vector<std::string> script;
    size_t script_pos = 0;
    int script_wait = 0;
    std::optional<Vec2> script_mouse;
    std::string screenshot_request;
    bool quit_requested = false;
    bool load_script(const std::string& path);
    void script_step();
    void handle_keyboard();
    void compute_snap();
    void apply_ortho_grid(Vec2& p) const;
    uint32_t entity_at(Vec2 p) const;
    void select_click(Vec2 p, bool shift);
    void select_window(Vec2 a, Vec2 b, bool shift);
    void on_double_click(uint32_t id);

    void draw_ribbon(float height);
    void draw_canvas(ImVec2 size);
    void draw_command_line();
    void draw_status_bar(float height);
    void draw_properties(float width, float height);
    void draw_popups();
    void draw_dynamic_input(ImDrawList* dl);
    void render_scene(ImDrawList* dl);
};

std::vector<std::string> prompt_keywords(const std::string& prompt);
std::string match_keyword(const std::string& prompt, const std::string& input);
bool parse_point(const std::string& s, Vec2 last, Vec2& out);
bool parse_number(const std::string& s, double& v);

// Rendering helpers (render.cpp)
ImU32 aci_color(int aci, float alpha = 1.0f);
void draw_entity(ImDrawList* dl, const Cad& c, const Entity& e, ImU32 col, float thick);
void draw_world_text(ImDrawList* dl, const Cad& c, const std::string& s, Vec2 ins, double height, double rot, ImU32 col, bool top_anchor, double wrap);
void draw_icon(ImDrawList* dl, const char* id, ImVec2 p, float sz, bool active);
void set_text_font(ImFont* f);
void apply_theme();

} // namespace cad
