#include "sim/autopilot.h"

#include <cmath>
#include <limits>

namespace gl {

namespace {

// The direction from `from` towards `to`, bent round the nearest pond, tree or
// building in the way.
Vec2 steer(const World& w, Vec2 from, Vec2 to) {
    const Vec2 d = to - from;
    const float len = length(d);
    if (len < 1e-4f) return {};
    const Vec2 u = d * (1.0f / len);
    float best = std::numeric_limits<float>::max();
    const Circle* block = nullptr;
    auto check = [&](const std::vector<Circle>& cs) {
        for (const Circle& c : cs) {
            const float r = c.r + kKeeperRadius + 0.5f;
            const Vec2 oc = c.c - from;
            const float t = oc.x * u.x + oc.z * u.z;
            if (t < 0 || t > len + r) continue;
            const Vec2 closest = from + u * t;
            if (distance(closest, c.c) >= r) continue;
            if (t < best) {
                best = t;
                block = &c;
            }
        }
    };
    check(w.map.ponds);
    check(w.map.trees);
    if (!block) return u;
    const Vec2 oc = block->c - from;
    const float dc = length(oc);
    const float r = block->r + kKeeperRadius + 0.6f;
    const float base = std::atan2(oc.z, oc.x);
    const float off = (dc > r ? std::asin(r / dc) : 1.5708f) + 0.15f;
    // Go round on the side the target is on.
    const float side = (oc.x * d.z - oc.z * d.x) >= 0 ? 1.0f : -1.0f;
    const float a = base + side * off;
    return {std::cos(a), std::sin(a)};
}

}  // namespace

Input autopilot(const World& w) {
    Input in;
    if (levelDone(w)) return in;
    const float far = (inFog(w, w.player) ? kFogFactor : 1.0f) * kLanternRadius * kFlareReach;
    int lured = -1, loose = -1, following = 0;
    float lureD = 1e9f, looseD = 1e9f;
    bool wispNear = false;
    for (const Wisp& s : w.wisps)
        if (s.dispelled <= 0 && distance(s.pos, w.player) < 6.0f) wispNear = true;
    for (size_t i = 0; i < w.ghosts.size(); ++i) {
        const Ghost& g = w.ghosts[i];
        const float d = distance(g.pos, w.player);
        if (g.state == GhostState::Lured && d < lureD) {
            lureD = d;
            lured = static_cast<int>(i);
        } else if ((g.state == GhostState::Idle || g.state == GhostState::Scared) && d < looseD) {
            looseD = d;
            loose = static_cast<int>(i);
        } else if (g.state == GhostState::Following) {
            ++following;
        }
    }
    if (flareReady(w) >= 1.0f && ((lured >= 0 && lureD < far) || (wispNear && following > 0))) in.flare = true;
    Vec2 target = w.map.home;
    if (lured >= 0)
        target = w.ghosts[static_cast<size_t>(lured)].pos;
    else if (loose >= 0)
        target = w.ghosts[static_cast<size_t>(loose)].pos;
    const float d = distance(target, w.player);
    if (d < 0.6f) return in;
    // Slow down near a ghost (followers keep up at a walk).
    const float pace = (lured >= 0 || loose >= 0) && d < 1.5f ? 0.5f : 0.95f;
    in.move = steer(w, w.player, target) * pace;
    return in;
}

bool playLevel(World& w, float seconds) {
    const int steps = static_cast<int>(seconds / kDt);
    for (int i = 0; i < steps && !levelDone(w); ++i) step(w, autopilot(w));
    return levelDone(w);
}

}  // namespace gl
