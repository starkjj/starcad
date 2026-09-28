#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace cad {

constexpr double PI  = 3.14159265358979323846;
constexpr double TAU = 2.0 * PI;
constexpr double EPS = 1e-9;

struct Vec2 {
    double x{}, y{};
    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    bool operator==(const Vec2&) const = default;
};

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double len(Vec2 a) { return std::sqrt(dot(a, a)); }
inline double dist(Vec2 a, Vec2 b) { return len(b - a); }
inline Vec2 perp(Vec2 a) { return {-a.y, a.x}; } // rotate +90 degrees (CCW)
inline Vec2 norm(Vec2 a) { double l = len(a); return l > EPS ? a / l : Vec2{1, 0}; }
inline Vec2 lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }
inline Vec2 dir(double ang) { return {std::cos(ang), std::sin(ang)}; }
inline double angle_of(Vec2 v) { return std::atan2(v.y, v.x); }
inline Vec2 rotate(Vec2 v, double ang) { double c = std::cos(ang), s = std::sin(ang); return {v.x * c - v.y * s, v.x * s + v.y * c}; }
inline bool near(Vec2 a, Vec2 b, double tol = 1e-6) { return dist(a, b) <= tol; }

// Normalize angle to [0, 2pi)
inline double norm_angle(double a) {
    a = std::fmod(a, TAU);
    if (a < 0) a += TAU;
    return a;
}

struct BBox {
    Vec2 mn{1e300, 1e300}, mx{-1e300, -1e300};
    bool valid() const { return mn.x <= mx.x && mn.y <= mx.y; }
    void add(Vec2 p) { mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); }
    void add(const BBox& b) { if (b.valid()) { add(b.mn); add(b.mx); } }
    bool contains(Vec2 p) const { return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y; }
    bool contains(const BBox& b) const { return b.valid() && contains(b.mn) && contains(b.mx); }
    bool overlaps(const BBox& b) const { return !(b.mn.x > mx.x || b.mx.x < mn.x || b.mn.y > mx.y || b.mx.y < mn.y); }
    Vec2 center() const { return (mn + mx) * 0.5; }
};

// Affine transform: p' = [a b; c d] * p + t
struct Xform {
    double a = 1, b = 0, c = 0, d = 1;
    Vec2 t{};
    Vec2 apply(Vec2 p) const { return {a * p.x + b * p.y + t.x, c * p.x + d * p.y + t.y}; }
    Vec2 apply_vec(Vec2 v) const { return {a * v.x + b * v.y, c * v.x + d * v.y}; }
    double det() const { return a * d - b * c; }
    double scale() const { return std::sqrt(std::fabs(det())); }
    bool mirrored() const { return det() < 0; }
    double rotation() const { return std::atan2(c, a); } // valid for similarity transforms

    static Xform translate(Vec2 v) { Xform x; x.t = v; return x; }
    static Xform rotate_about(Vec2 base, double ang) {
        Xform x; double cs = std::cos(ang), sn = std::sin(ang);
        x.a = cs; x.b = -sn; x.c = sn; x.d = cs;
        x.t = base - x.apply_vec(base);
        return x;
    }
    static Xform scale_about(Vec2 base, double s) {
        Xform x; x.a = s; x.d = s; x.t = base - base * s; return x;
    }
    static Xform mirror(Vec2 p1, Vec2 p2) {
        Vec2 u = norm(p2 - p1);
        Xform x; x.a = 2 * u.x * u.x - 1; x.b = 2 * u.x * u.y; x.c = x.b; x.d = 2 * u.y * u.y - 1;
        x.t = p1 - x.apply_vec(p1);
        return x;
    }
};

// ---------------------------------------------------------------------------
// Seg: a primitive curve piece — either a straight segment or a circular arc.
// All curve entities decompose into Segs for intersection, trim, extend and join.
// ---------------------------------------------------------------------------
struct Seg {
    Vec2 a, b;          // start / end point
    bool arc = false;
    Vec2 c;             // arc center
    double r = 0;       // arc radius
    double a0 = 0;      // arc start angle
    double sw = 0;      // arc signed sweep (positive = CCW)

    static Seg line(Vec2 a, Vec2 b) { Seg s; s.a = a; s.b = b; return s; }
    static Seg arc_of(Vec2 c, double r, double a0, double sw) {
        Seg s; s.arc = true; s.c = c; s.r = r; s.a0 = a0; s.sw = sw;
        s.a = c + dir(a0) * r; s.b = c + dir(a0 + sw) * r;
        return s;
    }

    Vec2 at(double t) const { return arc ? c + dir(a0 + sw * t) * r : lerp(a, b, t); }
    double length() const { return arc ? std::fabs(sw) * r : dist(a, b); }
    Vec2 tangent(double t) const { // unit direction of travel
        if (!arc) return norm(b - a);
        Vec2 radial = dir(a0 + sw * t);
        return sw >= 0 ? perp(radial) : -perp(radial);
    }
    Seg reversed() const {
        if (!arc) return line(b, a);
        return arc_of(c, r, a0 + sw, -sw);
    }
    // Sub-piece between parameters t0 < t1
    Seg sub(double t0, double t1) const {
        if (!arc) return line(at(t0), at(t1));
        return arc_of(c, r, a0 + sw * t0, sw * (t1 - t0));
    }
    double bulge() const { return arc ? std::tan(sw / 4.0) : 0.0; }
};

// Parameter of a point assumed to lie on the (extended) curve.
// For arcs, returns value relative to the sweep, may be outside [0,1].
inline double seg_param(const Seg& s, Vec2 p) {
    if (!s.arc) {
        Vec2 d = s.b - s.a;
        double l2 = dot(d, d);
        return l2 > EPS ? dot(p - s.a, d) / l2 : 0.0;
    }
    double ang = angle_of(p - s.c);
    double delta = s.sw >= 0 ? norm_angle(ang - s.a0) : norm_angle(s.a0 - ang);
    double sw = std::fabs(s.sw);
    if (sw < EPS) return 0;
    double t = delta / sw;
    // Put points in the "gap" to whichever end they are closer to (in angle)
    if (t > 1.0) {
        double over = delta - sw, under = TAU - delta;
        if (under < over) t = -under / sw;
    }
    return t;
}

inline double seg_closest_t(const Seg& s, Vec2 p) {
    if (!s.arc) return std::clamp(seg_param(s, p), 0.0, 1.0);
    if (dist(p, s.c) < EPS) return 0;
    return std::clamp(seg_param(s, p), 0.0, 1.0);
}

inline double seg_dist(const Seg& s, Vec2 p) { return dist(s.at(seg_closest_t(s, p)), p); }

// Circle through three points. Returns false if collinear.
inline bool circle_3p(Vec2 p1, Vec2 p2, Vec2 p3, Vec2& c, double& r) {
    double d = 2 * (p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y));
    if (std::fabs(d) < 1e-12) return false;
    double s1 = dot(p1, p1), s2 = dot(p2, p2), s3 = dot(p3, p3);
    c.x = (s1 * (p2.y - p3.y) + s2 * (p3.y - p1.y) + s3 * (p1.y - p2.y)) / d;
    c.y = (s1 * (p3.x - p2.x) + s2 * (p1.x - p3.x) + s3 * (p2.x - p1.x)) / d;
    r = dist(c, p1);
    return true;
}

// Arc through three points (start, via, end) as a Seg.
inline bool arc_3p(Vec2 p1, Vec2 p2, Vec2 p3, Seg& out) {
    Vec2 c; double r;
    if (!circle_3p(p1, p2, p3, c, r)) return false;
    double a1 = angle_of(p1 - c), a2 = angle_of(p2 - c), a3 = angle_of(p3 - c);
    double ccw13 = norm_angle(a3 - a1), ccw12 = norm_angle(a2 - a1);
    double sw = (ccw12 < ccw13) ? ccw13 : -(TAU - ccw13);
    out = Seg::arc_of(c, r, a1, sw);
    out.a = p1; out.b = p3;
    return true;
}

// Convert polyline bulge segment p->q to a Seg.
inline Seg bulge_seg(Vec2 p, Vec2 q, double bulge) {
    if (std::fabs(bulge) < 1e-12 || near(p, q, 1e-12)) return Seg::line(p, q);
    double L = dist(p, q);
    double sw = 4.0 * std::atan(bulge);
    double r = L * (1 + bulge * bulge) / (4 * std::fabs(bulge));
    double h = (L / 2) * (1 - bulge * bulge) / (2 * bulge);
    Vec2 m = (p + q) * 0.5;
    Vec2 n = perp(q - p) / L;
    Vec2 c = m + n * h;
    Seg s = Seg::arc_of(c, r, angle_of(p - c), sw);
    s.a = p; s.b = q; // exact endpoints
    return s;
}

// Line/line intersection of infinite lines. Returns params along each.
inline bool line_line(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2, double& t, double& u) {
    Vec2 r = a2 - a1, s = b2 - b1;
    double den = cross(r, s);
    if (std::fabs(den) < 1e-14 * std::max(1.0, len(r) * len(s))) return false;
    t = cross(b1 - a1, s) / den;
    u = cross(b1 - a1, r) / den;
    return true;
}

// Intersection points of an infinite line with a circle.
inline int line_circle(Vec2 a, Vec2 b, Vec2 c, double r, Vec2 out[2]) {
    Vec2 d = b - a;
    double A = dot(d, d);
    if (A < EPS * EPS) return 0;
    Vec2 f = a - c;
    double B = 2 * dot(f, d), C = dot(f, f) - r * r;
    double disc = B * B - 4 * A * C;
    if (disc < -1e-12 * A * r * r) return 0;
    if (disc < 0) disc = 0;
    double sq = std::sqrt(disc);
    double t1 = (-B - sq) / (2 * A), t2 = (-B + sq) / (2 * A);
    out[0] = a + d * t1;
    if (sq < 1e-12) return 1;
    out[1] = a + d * t2;
    return 2;
}

inline int circle_circle(Vec2 c1, double r1, Vec2 c2, double r2, Vec2 out[2]) {
    double d = dist(c1, c2);
    if (d < EPS || d > r1 + r2 + 1e-9 || d < std::fabs(r1 - r2) - 1e-9) return 0;
    double a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
    double h2 = r1 * r1 - a * a;
    double h = h2 > 0 ? std::sqrt(h2) : 0;
    Vec2 u = (c2 - c1) / d;
    Vec2 m = c1 + u * a;
    out[0] = m + perp(u) * h;
    if (h < 1e-12) return 1;
    out[1] = m - perp(u) * h;
    return 2;
}

struct Hit { double t, u; Vec2 p; };

// Intersections between two Segs within their parameter ranges (with small tolerance).
inline void seg_intersect(const Seg& A, const Seg& B, std::vector<Hit>& out, double tol = 1e-9) {
    auto in = [&](double t) { return t >= -tol && t <= 1 + tol; };
    Vec2 pts[2]; int n = 0;
    if (!A.arc && !B.arc) {
        double t, u;
        if (line_line(A.a, A.b, B.a, B.b, t, u) && in(t) && in(u)) out.push_back({t, u, A.at(t)});
        return;
    }
    if (!A.arc && B.arc) n = line_circle(A.a, A.b, B.c, B.r, pts);
    else if (A.arc && !B.arc) n = line_circle(B.a, B.b, A.c, A.r, pts);
    else n = circle_circle(A.c, A.r, B.c, B.r, pts);
    for (int i = 0; i < n; i++) {
        double t = seg_param(A, pts[i]), u = seg_param(B, pts[i]);
        if (in(t) && in(u)) out.push_back({t, u, pts[i]});
    }
}

// Tessellate a seg into points (includes start, excludes end unless last=true)
inline void seg_tessellate(const Seg& s, std::vector<Vec2>& out, int arc_steps, bool include_end) {
    if (!s.arc) {
        out.push_back(s.a);
    } else {
        int n = std::max(2, (int)std::ceil(arc_steps * std::fabs(s.sw) / TAU));
        for (int i = 0; i < n; i++) out.push_back(s.at((double)i / n));
    }
    if (include_end) out.push_back(s.b);
}

inline double polygon_area(const std::vector<Vec2>& p) {
    double a = 0;
    for (size_t i = 0, n = p.size(); i < n; i++) a += cross(p[i], p[(i + 1) % n]);
    return a * 0.5;
}

inline bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i]; const Vec2& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) in = !in;
    }
    return in;
}

inline double seg_point_dist(Vec2 a, Vec2 b, Vec2 p) {
    Vec2 d = b - a; double l2 = dot(d, d);
    double t = l2 > 0 ? std::clamp(dot(p - a, d) / l2, 0.0, 1.0) : 0.0;
    return dist(a + d * t, p);
}

inline bool segments_cross(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2) {
    double t, u;
    return line_line(a1, a2, b1, b2, t, u) && t >= 0 && t <= 1 && u >= 0 && u <= 1;
}

} // namespace cad
