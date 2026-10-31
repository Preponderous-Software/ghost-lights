#include "play/scene.h"

#include "sim/autopilot.h"

#include <algorithm>
#include <cmath>

namespace gl::play {

namespace {

constexpr float kPi = 3.14159265f;

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? a * (1.0f / l) : V3{0, 1, 0};
}
V3 at(Vec2 p, float y) { return {p.x, y, p.z}; }

struct Rgb {
    float r, g, b;
};

// Flat-shaded triangles for one static mesh.
struct Builder {
    std::vector<float> pos, nrm, col;
    std::vector<uint32_t> idx;

    void vert(V3 p, V3 n, Rgb c) {
        pos.insert(pos.end(), {p.x, p.y, p.z});
        nrm.insert(nrm.end(), {n.x, n.y, n.z});
        col.insert(col.end(), {c.r, c.g, c.b});
    }
    // A triangle whose front faces `out` (its winding is fixed to match).
    void tri(V3 a, V3 b, V3 c, V3 out, Rgb color) {
        V3 n = unit(cross(b - a, c - a));
        if (dot(n, out) < 0) {
            std::swap(b, c);
            n = n * -1.0f;
        }
        const auto base = static_cast<uint32_t>(pos.size() / 3);
        vert(a, n, color);
        vert(b, n, color);
        vert(c, n, color);
        idx.insert(idx.end(), {base, base + 1, base + 2});
    }
    // A flat quad a-b-c-d (in order round it) facing away from `inside`.
    void quad(V3 a, V3 b, V3 c, V3 d, V3 inside, Rgb color) {
        const V3 mid = (a + b + c + d) * 0.25f;
        V3 out = mid - inside;
        const V3 n = unit(cross(b - a, c - a));
        if (dot(out, out) < 1e-8f) out = n;
        tri(a, b, c, dot(n, out) >= 0 ? n : n * -1.0f, color);
        tri(a, c, d, dot(n, out) >= 0 ? n : n * -1.0f, color);
    }
    // A box from its centre and half sizes.
    void box(V3 c, V3 e, Rgb color) {
        V3 p[8];
        for (int i = 0; i < 8; ++i) p[i] = {c.x + ((i & 1) ? e.x : -e.x), c.y + ((i & 2) ? e.y : -e.y), c.z + ((i & 4) ? e.z : -e.z)};
        quad(p[0], p[1], p[3], p[2], c, color);  // -z
        quad(p[4], p[5], p[7], p[6], c, color);  // +z
        quad(p[0], p[2], p[6], p[4], c, color);  // -x
        quad(p[1], p[3], p[7], p[5], c, color);  // +x
        quad(p[2], p[3], p[7], p[6], c, color);  // +y
        quad(p[0], p[1], p[5], p[4], c, color);  // -y
    }
    // A square beam from a to b, `w` thick.
    void beam(V3 a, V3 b, float w, Rgb color) {
        const V3 d = unit(b - a);
        const V3 side = std::fabs(d.y) > 0.9f ? V3{1, 0, 0} : V3{0, 1, 0};
        const V3 p1 = unit(cross(d, side)) * (w * 0.5f), p2 = unit(cross(d, p1)) * (w * 0.5f);
        const V3 mid = (a + b) * 0.5f;
        const V3 ca[4] = {a + p1 + p2, a - p1 + p2, a - p1 - p2, a + p1 - p2};
        const V3 cb[4] = {b + p1 + p2, b - p1 + p2, b - p1 - p2, b + p1 - p2};
        for (int i = 0; i < 4; ++i) quad(ca[i], ca[(i + 1) % 4], cb[(i + 1) % 4], cb[i], mid, color);
        quad(cb[0], cb[1], cb[2], cb[3], mid, color);
    }
    // A flat disc facing up.
    void disc(V3 c, float r, int segments, Rgb color) {
        for (int i = 0; i < segments; ++i) {
            const float a0 = 2 * kPi * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = 2 * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
            tri(c, c + V3{std::cos(a0) * r, 0, std::sin(a0) * r}, c + V3{std::cos(a1) * r, 0, std::sin(a1) * r}, {0, 1, 0}, color);
        }
    }
    StrataMesh mesh() const {
        StrataMesh m{};
        m.positions = pos.data();
        m.normals = nrm.data();
        m.colors = col.data();
        m.indices = idx.data();
        m.vertex_count = static_cast<int32_t>(pos.size() / 3);
        m.index_count = static_cast<int32_t>(idx.size());
        m.color[0] = m.color[1] = m.color[2] = m.color[3] = 1;
        return m;
    }
    bool empty() const { return idx.empty(); }
};

// A small deterministic hash for scattering scenery.
float hash(float x, float z) {
    const float s = std::sin(x * 12.9898f + z * 78.233f) * 43758.5453f;
    return s - std::floor(s);
}

float smooth(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

float segDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float l2 = ab.x * ab.x + ab.z * ab.z;
    const float t = l2 > 0 ? std::clamp(((p.x - a.x) * ab.x + (p.z - a.z) * ab.z) / l2, 0.0f, 1.0f) : 0.0f;
    return distance(p, a + ab * t);
}

StrataRenderItem item(int kind, V3 p, V3 s, Rgb c) {
    StrataRenderItem it{};
    it.kind = kind;
    it.position[0] = p.x;
    it.position[1] = p.y;
    it.position[2] = p.z;
    it.scale[0] = s.x;
    it.scale[1] = s.y;
    it.scale[2] = s.z;
    it.color[0] = c.r;
    it.color[1] = c.g;
    it.color[2] = c.b;
    it.color[3] = 1;
    return it;
}

StrataPoint point(V3 p, float worldSize, float pixels, float r, float g, float b, float a, uint32_t flags) {
    StrataPoint pt{};
    pt.position[0] = p.x;
    pt.position[1] = p.y;
    pt.position[2] = p.z;
    pt.world_diameter = worldSize;
    pt.size_pixels = pixels;
    pt.color[0] = r;
    pt.color[1] = g;
    pt.color[2] = b;
    pt.color[3] = a;
    pt.flags = flags;
    return pt;
}

StrataPointLight light(V3 p, float range, float r, float g, float b, float intensity) {
    StrataPointLight l{};
    l.position[0] = p.x;
    l.position[1] = p.y;
    l.position[2] = p.z;
    l.range = range;
    l.color[0] = r;
    l.color[1] = g;
    l.color[2] = b;
    l.intensity = intensity;
    return l;
}

// The terrain's height: low hummocks, ponds sunk in, banks rising past the edge.
float groundHeight(const Level& l, Vec2 p) {
    float h = 0.08f * std::sin(p.x * 0.7f) * std::cos(p.z * 0.55f) + 0.05f * std::sin(p.x * 1.9f + p.z * 1.3f);
    for (const Circle& c : l.ponds) h -= 0.55f * smooth(c.r + 0.4f, c.r - 0.6f, distance(p, c.c));
    const float ex = std::max(std::fabs(p.x) - l.half.x, std::fabs(p.z) - l.half.z);
    h += 0.9f * smooth(-0.5f, 4.0f, ex);
    return h;
}

// One ghost, built round the origin: a round head, a body that flares to a
// wavy hem. Drawn unlit and translucent.
uint32_t addGhostMesh(strata::Engine& e) {
    const int seg = 18;
    const float prof[][2] = {{0.56f, 0.0f}, {0.53f, 0.14f}, {0.46f, 0.26f}, {0.34f, 0.35f}, {0.18f, 0.39f},
                             {0.0f, 0.40f},  {-0.22f, 0.42f}, {-0.42f, 0.47f}};
    const int rows = static_cast<int>(sizeof prof / sizeof prof[0]);
    std::vector<float> pos, col;
    std::vector<uint32_t> idx;
    for (int r = 0; r < rows; ++r)
        for (int s = 0; s <= seg; ++s) {
            const float a = 2 * kPi * static_cast<float>(s) / static_cast<float>(seg);
            float y = prof[r][0];
            if (r == rows - 1) y += 0.07f * std::sin(a * 5.0f);
            pos.insert(pos.end(), {std::cos(a) * prof[r][1], y, std::sin(a) * prof[r][1]});
            const float k = 0.6f + 0.4f * (y + 0.5f);
            col.insert(col.end(), {0.80f * k + 0.15f, 0.88f * k + 0.1f, 1.0f});
        }
    for (int r = 0; r + 1 < rows; ++r)
        for (int s = 0; s < seg; ++s) {
            const auto a = static_cast<uint32_t>(r * (seg + 1) + s), b = a + 1, c = a + seg + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    StrataTexturedMesh m{};
    m.mesh.positions = pos.data();
    m.mesh.colors = col.data();
    m.mesh.indices = idx.data();
    m.mesh.vertex_count = static_cast<int32_t>(pos.size() / 3);
    m.mesh.index_count = static_cast<int32_t>(idx.size());
    m.mesh.color[0] = m.mesh.color[1] = m.mesh.color[2] = 1;
    m.mesh.color[3] = 0.72f;
    m.flags = STRATA_MESH_UNLIT | STRATA_MESH_TRANSLUCENT;
    return e.addTexturedMesh(m);
}

void placeMesh(strata::Engine& e, uint32_t id, V3 p, float s, float sway) {
    float m[16] = {};
    const float c = std::cos(sway), sn = std::sin(sway);
    // translate * rotate about z (a gentle sway) * scale
    m[0] = c * s;
    m[1] = sn * s;
    m[4] = -sn * s;
    m[5] = c * s;
    m[10] = s;
    m[12] = p.x;
    m[13] = p.y;
    m[14] = p.z;
    m[15] = 1;
    e.setMeshTransform(id, m);
}

const Rgb kGrass{0.080f, 0.110f, 0.060f}, kGrassLight{0.115f, 0.140f, 0.075f}, kMud{0.090f, 0.075f, 0.058f},
    kPath{0.20f, 0.17f, 0.12f}, kBank{0.050f, 0.065f, 0.042f}, kReed{0.10f, 0.12f, 0.05f}, kTrunk{0.07f, 0.055f, 0.045f},
    kStone{0.22f, 0.22f, 0.24f}, kWall{0.26f, 0.22f, 0.18f}, kRoof{0.14f, 0.09f, 0.06f}, kWood{0.12f, 0.07f, 0.04f},
    kWarm{1.0f, 0.72f, 0.32f};

}  // namespace

Game::Game(strata::Engine& engine) : engine_(engine) {
    StrataLighting lt = strata::lightingDefaults(STRATA_LIGHTING_SUN);
    lt.cascade_count = 0;  // the moon casts no shadows; save the work
    lt.fog = 1;
    engine_.setLighting(lt);
    const float toSun[3] = {0.25f, -0.55f, 0.75f};  // well below the horizon: night, the moon high in the north
    engine_.setSunDirection(toSun);
    for (int i = 0; i < 12; ++i) ghostMeshes_.push_back(addGhostMesh(engine_));
    goToLevel(0);
}

void Game::goToLevel(int index) {
    const Events keep = world_.events;
    world_ = start(index, &keep);
    finished_ = false;
    carry_ = 0;
    cameraSet_ = false;
    build();
}

void Game::restart() { goToLevel(world_.level); }

void Game::next() {
    if (!levelDone(world_)) return;
    if (world_.level + 1 >= static_cast<int>(levels().size())) {
        finished_ = true;
        return;
    }
    goToLevel(world_.level + 1);
}

void Game::setMove(float x, float z, bool hurry) {
    keys_.move = {x, z};
    keys_.hurry = hurry;
}

void Game::setPointer(float px, float py, bool down, bool hurry) {
    pointerX_ = px;
    pointerY_ = py;
    pointerDown_ = down;
    pointerHurry_ = hurry;
}

void Game::build() {
    for (uint32_t id : levelMeshes_) engine_.removeMesh(id);
    levelMeshes_.clear();
    statics_.clear();
    fog_.clear();
    windows_.clear();
    const Level& l = world_.map;

    // ---- the ground
    const float margin = 9.0f, cell = 0.5f;
    const int sx = static_cast<int>((l.half.x + margin) * 2 / cell) + 1, sz = static_cast<int>((l.half.z + margin) * 2 / cell) + 1;
    std::vector<float> heights(static_cast<size_t>(sx * sz)), colors(static_cast<size_t>(sx * sz * 3));
    const Vec2 pathA = l.start, pathB = l.home;
    for (int j = 0; j < sz; ++j)
        for (int i = 0; i < sx; ++i) {
            const Vec2 p{-l.half.x - margin + static_cast<float>(i) * cell, -l.half.z - margin + static_cast<float>(j) * cell};
            const auto k = static_cast<size_t>(j * sx + i);
            heights[k] = groundHeight(l, p);
            const float n = hash(std::floor(p.x * 1.3f), std::floor(p.z * 1.3f));
            Rgb c{kGrass.r + (kGrassLight.r - kGrass.r) * n, kGrass.g + (kGrassLight.g - kGrass.g) * n, kGrass.b + (kGrassLight.b - kGrass.b) * n};
            float pondNear = 0;
            for (const Circle& pc : l.ponds) pondNear = std::max(pondNear, smooth(pc.r + 1.4f, pc.r, distance(p, pc.c)));
            const float wiggle = 0.6f * std::sin(p.z * 0.45f) + 0.4f * std::sin(p.z * 0.9f + 1.0f);
            const float onPath = smooth(1.1f, 0.5f, segDistance(p + Vec2{-wiggle, 0}, pathA, pathB));
            const float ex = std::max(std::fabs(p.x) - l.half.x, std::fabs(p.z) - l.half.z);
            const float bank = smooth(-1.0f, 2.0f, ex);
            auto mix = [](Rgb a, Rgb b, float t) { return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; };
            c = mix(c, kMud, pondNear);
            c = mix(c, kPath, onPath * (1 - pondNear));
            c = mix(c, kBank, bank);
            colors[k * 3] = c.r;
            colors[k * 3 + 1] = c.g;
            colors[k * 3 + 2] = c.b;
        }
    StrataTerrainTile tile{};
    tile.origin[0] = -l.half.x - margin;
    tile.origin[1] = 0;
    tile.origin[2] = -l.half.z - margin;
    tile.cell_size = cell;
    tile.samples_x = sx;
    tile.samples_z = sz;
    tile.heights = heights.data();
    tile.colors = colors.data();
    engine_.setTerrain(&tile);

    Builder b, glow;
    // ---- ponds: dark water, reeds round the edge
    for (const Circle& c : l.ponds) {
        b.disc(at(c.c, -0.12f), c.r + 0.2f, 28, {0.030f, 0.055f, 0.095f});
        const int reeds = static_cast<int>(c.r * 9);
        for (int i = 0; i < reeds; ++i) {
            const float a = 2 * kPi * (static_cast<float>(i) + hash(c.c.x, static_cast<float>(i)) * 0.6f) / static_cast<float>(reeds);
            const float rr = c.r + 0.1f + 0.5f * hash(static_cast<float>(i), c.c.z);
            const V3 base{c.c.x + std::cos(a) * rr, -0.05f, c.c.z + std::sin(a) * rr};
            const float h = 0.8f + 0.7f * hash(base.x, base.z);
            const V3 top = base + V3{0.15f * std::cos(a), h, 0.15f * std::sin(a)};
            b.beam(base, top, 0.05f, kReed);
            if (i % 3 == 0) statics_.push_back(item(STRATA_PRIMITIVE_CAPSULE, top + V3{0, -0.12f, 0}, {0.09f, 0.11f, 0.09f}, {0.16f, 0.09f, 0.05f}));
        }
    }
    // ---- bare trees
    const std::vector<Circle> blds = buildings(l);
    for (const Circle& c : l.trees) {
        const bool building =
            std::find_if(blds.begin(), blds.end(), [&](const Circle& o) { return distance(o.c, c.c) < 0.01f; }) != blds.end();
        if (building) continue;
        const V3 base = at(c.c, -0.1f);
        const float h = 3.2f + hash(c.c.x, c.c.z) * 1.2f;
        b.beam(base, base + V3{0.1f, h, 0.05f}, 0.38f, kTrunk);
        for (int k = 0; k < 5; ++k) {
            const float a = static_cast<float>(k) * 2.3f + c.c.x;
            const float y = h * (0.45f + 0.11f * static_cast<float>(k));
            const V3 from = base + V3{0.05f, y, 0};
            const V3 to = from + V3{std::cos(a) * 1.3f, 0.7f + 0.2f * static_cast<float>(k % 2), std::sin(a) * 1.0f};
            b.beam(from, to, 0.13f, kTrunk);
            b.beam(to, to + V3{std::cos(a + 0.8f) * 0.6f, 0.45f, std::sin(a + 0.8f) * 0.5f}, 0.07f, kTrunk);
        }
    }
    // ---- reeds and dead trees on the banks past the edge
    for (float t = -l.half.x - 2; t <= l.half.x + 2; t += 1.1f)
        for (int side = -1; side <= 1; side += 2)
            for (int row = 0; row < 2; ++row) {
                const V3 base{t + hash(t, static_cast<float>(side + row)) * 0.8f, 0.2f, side * (l.half.z + 0.8f + static_cast<float>(row) * 1.3f)};
                if (l.homeKind == HomeKind::Churchyard && side < 0) continue;
                const float h = 1.0f + hash(base.x, base.z) * 0.9f;
                b.beam(base, base + V3{0.1f, h, 0.1f * static_cast<float>(side)}, 0.06f, kReed);
            }
    for (float t = -l.half.z - 2; t <= l.half.z + 2; t += 1.1f)
        for (int side = -1; side <= 1; side += 2)
            for (int row = 0; row < 2; ++row) {
                const V3 base{side * (l.half.x + 0.8f + static_cast<float>(row) * 1.3f), 0.2f, t + hash(static_cast<float>(side + row), t) * 0.8f};
                const float h = 1.0f + hash(base.z, base.x) * 0.9f;
                b.beam(base, base + V3{0.1f * static_cast<float>(side), h, 0.1f}, 0.06f, kReed);
            }

    // ---- home
    const Vec2 h = l.home;
    if (l.homeKind == HomeKind::Cottage) {
        const V3 c = at(h + Vec2{0, -3.8f}, 0);
        b.box(c + V3{0, 1.3f, 0}, {2.5f, 1.3f, 1.8f}, kWall);
        // The roof: two slopes and two gables.
        const V3 e1 = c + V3{-2.8f, 2.6f, 2.1f}, e2 = c + V3{2.8f, 2.6f, 2.1f}, e3 = c + V3{2.8f, 2.6f, -2.1f}, e4 = c + V3{-2.8f, 2.6f, -2.1f};
        const V3 r1 = c + V3{-2.8f, 4.3f, 0}, r2 = c + V3{2.8f, 4.3f, 0};
        const V3 mid = c + V3{0, 3.0f, 0};
        b.quad(e1, e2, r2, r1, mid, kRoof);
        b.quad(e4, e3, r2, r1, mid, kRoof);
        b.tri(c + V3{-2.5f, 2.6f, 1.8f}, c + V3{-2.5f, 2.6f, -1.8f}, c + V3{-2.5f, 4.1f, 0}, {-1, 0, 0}, kWall);
        b.tri(c + V3{2.5f, 2.6f, 1.8f}, c + V3{2.5f, 2.6f, -1.8f}, c + V3{2.5f, 4.1f, 0}, {1, 0, 0}, kWall);
        b.box(c + V3{1.4f, 4.2f, -0.6f}, {0.3f, 0.8f, 0.3f}, kStone);
        // The door stands open, warm light inside; two lit windows.
        const float fz = c.z + 1.82f;
        glow.quad({c.x - 0.5f, 0.0f, fz}, {c.x + 0.5f, 0.0f, fz}, {c.x + 0.5f, 1.9f, fz}, {c.x - 0.5f, 1.9f, fz}, {c.x, 1.0f, fz - 1}, kWarm);
        b.box({c.x - 0.75f, 0.95f, fz + 0.35f}, {0.05f, 0.95f, 0.38f}, kWood);  // the open door
        for (int s = -1; s <= 1; s += 2) {
            const float wx = c.x + 1.6f * static_cast<float>(s);
            glow.quad({wx - 0.4f, 1.0f, fz}, {wx + 0.4f, 1.0f, fz}, {wx + 0.4f, 1.7f, fz}, {wx - 0.4f, 1.7f, fz}, {wx, 1.3f, fz - 1},
                      {1.0f, 0.8f, 0.4f});
            b.box({wx, 1.35f, fz + 0.03f}, {0.03f, 0.36f, 0.02f}, kWood);
            b.box({wx, 1.35f, fz + 0.03f}, {0.41f, 0.03f, 0.02f}, kWood);
            windows_.push_back({wx, fz - 0.05f});
        }
        windowY_ = 1.35f;
        // Pumpkins by the door, lit from inside.
        for (int s = -1; s <= 1; s += 2) {
            const V3 pp{c.x + 1.0f * static_cast<float>(s), 0.22f, fz + 0.6f};
            statics_.push_back(item(STRATA_PRIMITIVE_SPHERE, pp, {0.5f, 0.4f, 0.5f}, {0.55f, 0.20f, 0.03f}));
            statics_.push_back(item(STRATA_PRIMITIVE_CYLINDER, pp + V3{0, 0.24f, 0}, {0.06f, 0.07f, 0.06f}, {0.1f, 0.12f, 0.04f}));
        }
        // A fence each side of the yard.
        for (int s = -1; s <= 1; s += 2) {
            for (int k = 0; k < 5; ++k) {
                const V3 post{c.x + static_cast<float>(s) * (3.2f + static_cast<float>(k) * 1.2f), 0.45f, fz + 0.6f};
                b.box(post, {0.07f, 0.5f, 0.07f}, kWood);
            }
            b.beam({c.x + static_cast<float>(s) * 3.2f, 0.65f, fz + 0.6f}, {c.x + static_cast<float>(s) * 8.0f, 0.65f, fz + 0.6f}, 0.06f, kWood);
        }
    } else {
        // The churchyard: a stone wall along the north edge, an open gate,
        // gravestones and a small church beyond.
        const float wz = h.z - 2.3f;
        for (int s = -1; s <= 1; s += 2) {
            const float x0 = static_cast<float>(s) * 2.2f, x1 = static_cast<float>(s) * (l.half.x + 6);
            b.box({(x0 + x1) / 2, 0.6f, wz}, {std::fabs(x1 - x0) / 2, 0.6f, 0.3f}, kStone);
            b.box({static_cast<float>(s) * 1.7f, 1.2f, wz}, {0.45f, 1.2f, 0.45f}, kStone);
            b.box({static_cast<float>(s) * 1.7f, 2.5f, wz}, {0.55f, 0.1f, 0.55f}, kStone);
            glow.box({static_cast<float>(s) * 1.7f, 2.85f, wz}, {0.15f, 0.22f, 0.15f}, kWarm);
        }
        b.beam({-1.7f, 2.6f, wz}, {1.7f, 2.6f, wz}, 0.25f, kWood);
        b.box({-1.0f, 0.9f, wz - 0.6f}, {0.05f, 0.9f, 0.55f}, kWood);  // the gate, swung open
        b.box({1.0f, 0.9f, wz - 0.6f}, {0.05f, 0.9f, 0.55f}, kWood);
        for (int k = 0; k < 16; ++k) {
            const float gx = (hash(static_cast<float>(k), 3.0f) - 0.5f) * 26.0f;
            const float gz = wz - 2.0f - hash(5.0f, static_cast<float>(k)) * 6.0f;
            if (std::fabs(gx) < 1.8f) continue;
            const float tall = 0.5f + hash(gx, gz) * 0.4f;
            b.box({gx, tall / 2, gz}, {0.35f, tall / 2, 0.09f}, kStone);
            statics_.push_back(item(STRATA_PRIMITIVE_CYLINDER, {gx, tall, gz}, {0.7f, 0.18f, 0.7f}, kStone));
        }
        const V3 ch{0, 0, wz - 11.0f};
        b.box(ch + V3{0, 2.2f, 0}, {3.0f, 2.2f, 4.0f}, kWall);
        b.quad(ch + V3{-3.3f, 4.4f, 4.2f}, ch + V3{-3.3f, 4.4f, -4.2f}, ch + V3{0, 6.4f, -4.2f}, ch + V3{0, 6.4f, 4.2f}, ch + V3{0, 5, 0}, kRoof);
        b.quad(ch + V3{3.3f, 4.4f, 4.2f}, ch + V3{3.3f, 4.4f, -4.2f}, ch + V3{0, 6.4f, -4.2f}, ch + V3{0, 6.4f, 4.2f}, ch + V3{0, 5, 0}, kRoof);
        b.box(ch + V3{0, 5.5f, 4.6f}, {1.0f, 5.5f, 1.0f}, kWall);
        b.beam(ch + V3{0, 11.0f, 4.6f}, ch + V3{0, 13.5f, 4.6f}, 1.2f, kRoof);
        for (int s = -1; s <= 1; s += 2) {
            const float wx = static_cast<float>(s) * 3.02f;
            for (int k = 0; k < 2; ++k) {
                const float wzk = ch.z - 1.5f + static_cast<float>(k) * 3.0f;
                glow.quad({wx, 1.6f, wzk - 0.4f}, {wx, 1.6f, wzk + 0.4f}, {wx, 3.0f, wzk + 0.4f}, {wx, 3.0f, wzk - 0.4f}, {0, 2, wzk},
                          {1.0f, 0.78f, 0.38f});
            }
        }
        glow.quad({-0.5f, 0.0f, ch.z + 5.62f}, {0.5f, 0.0f, ch.z + 5.62f}, {0.5f, 2.2f, ch.z + 5.62f}, {-0.5f, 2.2f, ch.z + 5.62f},
                  ch + V3{0, 1, 4}, kWarm);
        windows_.push_back({-1.7f, wz});
        windows_.push_back({1.7f, wz});
        windowY_ = 3.2f;
    }
    if (!b.empty()) levelMeshes_.push_back(engine_.addMesh(b.mesh()));
    if (!glow.empty()) {
        StrataTexturedMesh t{};
        t.mesh = glow.mesh();
        t.flags = STRATA_MESH_UNLIT;
        levelMeshes_.push_back(engine_.addTexturedMesh(t));
    }

    // ---- fog: puffs in every bank, and a thin mist over everything
    for (size_t f = 0; f < l.fog.size(); ++f) {
        const Circle& c = l.fog[f];
        const int n = static_cast<int>(c.r * c.r * 1.4f);
        for (int i = 0; i < n; ++i) {
            const float a = hash(static_cast<float>(i), static_cast<float>(f) + 0.5f) * 2 * kPi;
            const float rr = std::sqrt(hash(static_cast<float>(f) + 2.0f, static_cast<float>(i))) * c.r * 0.95f;
            fog_.push_back({c.c.x + std::cos(a) * rr, 0.3f + hash(rr, a) * 1.4f, c.c.z + std::sin(a) * rr,
                            3.0f + hash(a, rr) * 2.5f, 0.20f + hash(rr + 1, a) * 0.10f, 0.3f + hash(a + 2, rr) * 0.4f});
        }
    }
    for (float x = -l.half.x - 4; x <= l.half.x + 4; x += 4.5f)
        for (float z = -l.half.z - 4; z <= l.half.z + 4; z += 4.5f)
            fog_.push_back({x + hash(x, z) * 3, 0.25f + hash(z, x) * 0.6f, z + hash(x + 1, z) * 3, 6.0f + hash(z + 1, x) * 3, 0.11f, 0.4f});
}

void Game::camera(float aspect) {
    const Vec2 p = world_.player;
    const float zoom = aspect < 1.1f ? std::clamp(1.15f / aspect, 1.0f, 2.3f) : 1.0f;
    const float tx = p.x, tz = p.z - 1.2f;
    const float ex = tx, ey = 13.0f * zoom, ez = tz + 10.0f * zoom;
    if (!cameraSet_) {
        eye_[0] = ex, eye_[1] = ey, eye_[2] = ez;
        look_[0] = tx, look_[1] = 0, look_[2] = tz;
        cameraSet_ = true;
    } else {
        const float k = 0.12f;
        eye_[0] += (ex - eye_[0]) * k;
        eye_[1] += (ey - eye_[1]) * k;
        eye_[2] += (ez - eye_[2]) * k;
        look_[0] += (tx - look_[0]) * k;
        look_[2] += (tz - look_[2]) * k;
    }
    aspect_ = aspect;
    const float up[3] = {0, 1, 0};
    const float dist = std::sqrt(ey * ey + (ez - tz) * (ez - tz));
    engine_.setCamera(strata::cameraLookAt(eye_, look_, up, fov_, aspect, 0.3f, dist + 28.0f));
}

void Game::frame(double seconds) {
    clock_ += static_cast<float>(seconds);
    // The pointer: walk towards the ground under it.
    Input in = keys_;
    if (pointerDown_ && cameraSet_ && length(keys_.move) < 0.01f) {
        const V3 eye{eye_[0], eye_[1], eye_[2]}, look{look_[0], look_[1], look_[2]};
        const V3 fwd = unit(look - eye), right = unit(cross(fwd, {0, 1, 0})), up = cross(right, fwd);
        const float t = std::tan(fov_ * kPi / 360.0f);
        const float nx = pointerX_ * 2 - 1, ny = 1 - pointerY_ * 2;
        const V3 dir = unit(fwd + right * (nx * t * aspect_) + up * (ny * t));
        if (dir.y < -0.01f) {
            const float k = -eye.y / dir.y;
            const Vec2 ground{eye.x + dir.x * k, eye.z + dir.z * k};
            const Vec2 d = ground - world_.player;
            const float l = length(d);
            if (l > 0.25f) in.move = normalized(d) * std::min(1.0f, l / 1.5f);
            in.hurry = pointerHurry_;
        }
    }
    in.flare = flareAsked_;
    flareAsked_ = false;
    if (!paused_ && !finished_) {
        carry_ = std::min(carry_ + seconds, 0.25);
        bool first = true;
        while (carry_ >= kDt) {
            Input use = autopilot_ ? autopilot(world_) : in;
            if (!first) use.flare = autopilot_ ? use.flare : false;
            step(world_, use);
            first = false;
            carry_ -= kDt;
        }
        if (first && in.flare) flareAsked_ = true;  // no step this frame: keep the flare for the next
    }
    int32_t w = 1, h = 1;
    engine_.drawableSize(w, h);
    camera(h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f);
    draw();
}

void Game::draw() {
    const World& w = world_;
    const Level& l = w.map;
    const float t = clock_;
    const V3 keeper = at(w.player, 0);
    const V3 side{-w.facing.z, 0, w.facing.x};
    const float bob = length(w.vel) > 0.3f ? std::fabs(std::sin(w.time * 9.0f)) * 0.05f : 0.0f;
    const V3 lamp = keeper + side * 0.42f + V3{w.facing.x * 0.25f, 0.95f + bob, w.facing.z * 0.25f};
    const float r = reach(w);
    const float flick = 1.0f + 0.05f * std::sin(t * 13.0f) + 0.04f * std::sin(t * 7.3f);
    const bool hurrying = w.fastFor > 0;

    // ---- lights
    std::vector<StrataPointLight> lights;
    lights.push_back(light(lamp + V3{0, 0.4f, 0}, r * 2.6f + 2.0f, 1.0f, 0.62f, 0.28f, (w.flare > 0 ? 3.6f : 2.3f) * flick));
    const Vec2 h = l.home;
    if (l.homeKind == HomeKind::Cottage) {
        lights.push_back(light(at(h + Vec2{0, -1.2f}, 1.2f), 8.0f, 1.0f, 0.7f, 0.35f, 1.2f));
        lights.push_back(light(at(h + Vec2{0, -1.4f}, 0.5f), 3.5f, 1.0f, 0.45f, 0.1f, 0.7f * flick));  // the pumpkins
    } else {
        for (int s = -1; s <= 1; s += 2) lights.push_back(light({static_cast<float>(s) * 1.7f, 3.0f, h.z - 2.3f}, 7.0f, 1.0f, 0.7f, 0.35f, 1.1f * flick));
    }
    for (const Ghost& g : w.ghosts)
        if (g.state != GhostState::Home && lights.size() < 26)
            lights.push_back(light(at(g.pos, g.height), 3.2f, 0.55f, 0.7f, 1.0f, 0.45f));
    for (const Wisp& s : w.wisps)
        if (s.dispelled <= 0 && lights.size() < 30) lights.push_back(light(at(s.pos, 1.0f), 4.5f, 0.35f, 1.0f, 0.7f, 0.9f * flick));
    // A faint fill from the sky, so shapes keep their edges in the dark.
    lights.push_back(light({w.player.x, 40.0f, w.player.z + 8}, 140.0f, 0.45f, 0.55f, 1.0f, 1.15f));
    engine_.setPointLights(lights.data(), lights.size());

    // ---- ghosts
    for (size_t i = 0; i < ghostMeshes_.size(); ++i) {
        if (i >= w.ghosts.size() || w.ghosts[i].state == GhostState::Home) {
            placeMesh(engine_, ghostMeshes_[i], {0, -50, 0}, 0.001f, 0);
            continue;
        }
        const Ghost& g = w.ghosts[i];
        float s = 1.0f;
        if (g.state == GhostState::GoingHome) s = std::max(0.05f, 1.0f - g.timer / kGoingHomeTime);
        const float sway = std::clamp(-g.vel.x * 0.08f, -0.3f, 0.3f) + 0.05f * std::sin(g.phase * 1.7f);
        placeMesh(engine_, ghostMeshes_[i], at(g.pos, g.height), s * 0.95f, sway);
    }

    StrataFrameDesc fd{};
    fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | STRATA_CLEAR_SKY;
    fd.clear_color[3] = 1;
    engine_.beginFrame(fd);

    // ---- the keeper, and everything else made of primitives
    std::vector<StrataRenderItem> items = statics_;
    const Rgb cloak{0.16f, 0.10f, 0.20f};
    items.push_back(item(STRATA_PRIMITIVE_CAPSULE, keeper + V3{0, 0.62f + bob, 0}, {0.55f, 0.5f, 0.55f}, cloak));
    items.push_back(item(STRATA_PRIMITIVE_SPHERE, keeper + V3{0, 1.42f + bob, 0}, {0.42f, 0.42f, 0.42f}, {0.62f, 0.46f, 0.36f}));
    items.push_back(item(STRATA_PRIMITIVE_CYLINDER, keeper + V3{0, 1.62f + bob, 0}, {0.78f, 0.03f, 0.78f}, {0.08f, 0.06f, 0.10f}));
    items.push_back(item(STRATA_PRIMITIVE_CYLINDER, keeper + V3{0, 1.85f + bob, 0}, {0.36f, 0.22f, 0.36f}, {0.08f, 0.06f, 0.10f}));
    items.push_back(item(STRATA_PRIMITIVE_CYLINDER, keeper + V3{0, 2.12f + bob, 0}, {0.18f, 0.1f, 0.18f}, {0.08f, 0.06f, 0.10f}));
    items.push_back(item(STRATA_PRIMITIVE_CUBE, lamp, {0.2f, 0.26f, 0.2f}, {0.25f, 0.18f, 0.08f}));
    items.push_back(item(STRATA_PRIMITIVE_CYLINDER, lamp + V3{0, 0.2f, 0}, {0.03f, 0.08f, 0.03f}, {0.1f, 0.1f, 0.1f}));
    engine_.submitPrimitives(items.data(), items.size());

    std::vector<StrataPoint> pts;
    const uint32_t soft = STRATA_POINT_SOFT;
    // The lantern's flame and halo.
    pts.push_back(point(lamp, 0.2f, 3, 1.0f, 0.9f, 0.6f, 1.0f, 0));
    pts.push_back(point(lamp, 2.0f * flick + (w.flare > 0 ? 3.0f : 0), 0, 1.0f, 0.7f, 0.3f, 0.6f, soft));
    pts.push_back(point(lamp + V3{0, -0.8f, 0}, r * 1.3f, 0, 1.0f, 0.6f, 0.25f, w.flare > 0 ? 0.22f : 0.10f, soft));
    // The door's glow, pulsing gently: home.
    {
        const float pulse = 0.5f + 0.5f * std::sin(t * 2.0f);
        const V3 door = l.homeKind == HomeKind::Cottage ? at(h + Vec2{0, -1.9f}, 1.0f) : at(h + Vec2{0, -2.3f}, 1.2f);
        pts.push_back(point(door, 3.5f + pulse * 0.8f, 0, 1.0f, 0.7f, 0.35f, 0.25f + 0.1f * pulse, soft));
        for (size_t i = 0; i < windows_.size(); ++i)
            pts.push_back(point(at(windows_[i], windowY_), 1.6f, 0, 1.0f, 0.75f, 0.4f, 0.25f, soft));
    }
    // The moon on the ponds.
    for (const Circle& c : l.ponds)
        pts.push_back(point(at(c.c + Vec2{0.3f, -0.4f}, -0.05f), c.r * 0.9f, 0, 0.55f, 0.65f, 0.9f, 0.10f + 0.02f * std::sin(t * 0.7f + c.c.x), soft));
    // Ghosts already home peek out at the windows.
    {
        int home = 0;
        for (const Ghost& g : w.ghosts)
            if (g.state == GhostState::Home && !windows_.empty()) {
                const Vec2 wp = windows_[static_cast<size_t>(home) % windows_.size()];
                const float off = (static_cast<float>(home / static_cast<int>(windows_.size())) - 1.0f) * 0.22f;
                const V3 c = at(wp + Vec2{off, 0.12f}, windowY_ + 0.05f * std::sin(t * 2 + static_cast<float>(home)));
                pts.push_back(point(c, 0.32f, 0, 0.92f, 0.96f, 1.0f, 0.95f, soft));
                ++home;
            }
    }
    // Ghost glows and faces. The camera looks north and down: faces turn to it.
    const V3 toCam = unit(V3{eye_[0], eye_[1], eye_[2]} - V3{look_[0], look_[1], look_[2]});
    for (const Ghost& g : w.ghosts) {
        if (g.state == GhostState::Home) continue;
        float s = 1.0f;
        if (g.state == GhostState::GoingHome) s = std::max(0.05f, 1.0f - g.timer / kGoingHomeTime);
        const V3 c = at(g.pos, g.height);
        float gr = 0.55f, gg = 0.72f, gb = 1.0f, ga = 0.28f;
        if (g.state == GhostState::Following) gr = 0.9f, gg = 0.8f, gb = 0.65f, ga = 0.30f;
        if (g.state == GhostState::Lured) gr = 0.35f, gg = 1.0f, gb = 0.7f, ga = 0.34f;
        if (g.state == GhostState::Scared) ga = 0.15f + 0.15f * std::fabs(std::sin(g.phase * 20));
        pts.push_back(point(c, 2.6f * s, 0, gr, gg, gb, ga * 1.3f, soft));
        const V3 face = c + V3{0, 0.30f * s, 0} + toCam * (0.36f * s);
        const bool blink = std::fmod(g.phase * 0.9f + static_cast<float>(&g - w.ghosts.data()) * 0.37f, 4.0f) < 0.12f;
        const float eyeSize = (g.state == GhostState::Scared ? 0.15f : 0.11f) * s;
        if (!blink)
            for (int e = -1; e <= 1; e += 2)
                pts.push_back(point(face + V3{0.12f * s * static_cast<float>(e), 0, 0}, eyeSize, 0, 0.03f, 0.03f, 0.08f, 1.0f, 0));
        if (g.state == GhostState::Scared || g.state == GhostState::Lured)
            pts.push_back(point(face + V3{0, -0.15f * s, 0.02f}, 0.08f * s, 0, 0.03f, 0.03f, 0.08f, 1.0f, 0));
    }
    // Wisps: a bright core, a halo, a short trail.
    for (size_t i = 0; i < w.wisps.size(); ++i) {
        const Wisp& s = w.wisps[i];
        if (s.dispelled > 0) continue;
        const float f = 0.75f + 0.25f * std::sin(t * 11.0f + static_cast<float>(i) * 2.0f);
        const V3 c = at(s.pos, 1.0f + 0.25f * std::sin(t * 2.3f + static_cast<float>(i)));
        pts.push_back(point(c, 0.28f, 3, 0.8f, 1.0f, 0.9f, 1.0f, 0));
        pts.push_back(point(c, 1.6f * f, 0, 0.3f, 1.0f, 0.7f, 0.45f, soft));
        for (int k = 1; k <= 4; ++k) {
            const float back = static_cast<float>(k) * 0.35f;
            const V3 tc = c + V3{std::sin(t * 3 + static_cast<float>(k)) * 0.1f * back, -0.05f * back, std::cos(t * 2 + static_cast<float>(k)) * 0.1f * back};
            pts.push_back(point(tc, 0.5f - 0.08f * static_cast<float>(k), 0, 0.3f, 1.0f, 0.75f, 0.3f - 0.06f * static_cast<float>(k), soft));
        }
    }
    // Fireflies.
    for (int i = 0; i < 14; ++i) {
        const float fx = (hash(static_cast<float>(i), 1.0f) - 0.5f) * l.half.x * 1.8f + std::sin(t * 0.3f + static_cast<float>(i)) * 1.5f;
        const float fz = (hash(2.0f, static_cast<float>(i)) - 0.5f) * l.half.z * 1.8f + std::cos(t * 0.25f + static_cast<float>(i) * 1.3f) * 1.5f;
        const float on = std::max(0.0f, std::sin(t * 1.3f + static_cast<float>(i) * 2.1f));
        if (on > 0.05f) pts.push_back(point({fx, 0.7f + 0.3f * std::sin(t + static_cast<float>(i)), fz}, 0.25f, 2, 0.9f, 1.0f, 0.4f, on * 0.8f, soft));
    }
    // Fog, lit warm where the lantern reaches it.
    for (const Puff& p : fog_) {
        const float dx = p.x + std::sin(t * 0.1f * p.drift + p.z) * 0.8f, dz = p.z + std::cos(t * 0.08f * p.drift + p.x) * 0.6f;
        const float d = std::sqrt((dx - lamp.x) * (dx - lamp.x) + (dz - lamp.z) * (dz - lamp.z));
        const float k = std::pow(std::max(0.0f, 1.0f - d / (r * 1.9f + 1.5f)), 2.0f);
        pts.push_back(point({dx, p.y, dz}, p.size, 0, 0.30f + 0.7f * k, 0.33f + 0.42f * k, 0.42f + 0.1f * k, p.alpha * (1.0f + 0.6f * k), soft));
    }
    engine_.submitPoints(pts.data(), pts.size());

    // ---- the light's reach, faintly on the ground (red while hurrying)
    std::vector<StrataLineVertex> ring;
    const int segs = 56;
    for (int i = 0; i < segs; ++i) {
        if (i % 2) continue;
        for (int k = 0; k < 2; ++k) {
            const float a = 2 * kPi * static_cast<float>(i + k) / static_cast<float>(segs);
            const Vec2 q = w.player + Vec2{std::cos(a) * r, std::sin(a) * r};
            StrataLineVertex v{};
            v.position[0] = q.x;
            v.position[1] = groundHeight(l, q) + 0.06f;
            v.position[2] = q.z;
            v.color[0] = 1.0f;
            v.color[1] = hurrying ? 0.3f : 0.72f;
            v.color[2] = hurrying ? 0.2f : 0.35f;
            v.color[3] = hurrying ? 0.5f : 0.22f;
            ring.push_back(v);
        }
    }
    // Home's doorstep, a ring of small lights.
    for (int i = 0; i < 24; ++i) {
        if (i % 2) continue;
        for (int k = 0; k < 2; ++k) {
            const float a = 2 * kPi * static_cast<float>(i + k) / 24.0f;
            const Vec2 q = h + Vec2{std::cos(a) * l.homeRadius, std::sin(a) * l.homeRadius};
            StrataLineVertex v{};
            v.position[0] = q.x;
            v.position[1] = groundHeight(l, q) + 0.06f;
            v.position[2] = q.z;
            v.color[0] = 1.0f;
            v.color[1] = 0.8f;
            v.color[2] = 0.45f;
            v.color[3] = 0.35f + 0.15f * std::sin(t * 2.0f);
            ring.push_back(v);
        }
    }
    engine_.submitLines(ring.data(), ring.size());
    engine_.endFrame();
}

}  // namespace gl::play
