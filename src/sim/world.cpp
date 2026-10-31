#include "sim/world.h"

#include <algorithm>
#include <cmath>

namespace gl {

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 normalized(Vec2 v) {
    const float l = length(v);
    return l > 1e-6f ? v * (1.0f / l) : Vec2{};
}

namespace {

float rand01(World& w) {
    // xorshift64*: the same numbers on every compiler.
    w.rng ^= w.rng >> 12;
    w.rng ^= w.rng << 25;
    w.rng ^= w.rng >> 27;
    const uint64_t r = w.rng * 0x2545F4914F6CDD1Dull;
    return static_cast<float>(r >> 40) / static_cast<float>(1ull << 24);
}

bool inAny(const std::vector<Circle>& cs, Vec2 p) {
    for (const Circle& c : cs)
        if (distance(p, c.c) < c.r) return true;
    return false;
}

float loopLength(const WispPath& p) {
    float total = 0;
    for (size_t i = 0; i < p.points.size(); ++i) total += distance(p.points[i], p.points[(i + 1) % p.points.size()]);
    return total;
}

Vec2 alongLoop(const WispPath& p, float d) {
    if (p.points.empty()) return {};
    const float total = loopLength(p);
    if (total <= 0) return p.points[0];
    d = std::fmod(d, total);
    if (d < 0) d += total;
    for (size_t i = 0; i < p.points.size(); ++i) {
        const Vec2 a = p.points[i], b = p.points[(i + 1) % p.points.size()];
        const float seg = distance(a, b);
        if (d <= seg && seg > 0) return a + (b - a) * (d / seg);
        d -= seg;
    }
    return p.points[0];
}

Vec2 clampTo(const World& w, Vec2 p, float margin) {
    p.x = std::clamp(p.x, -w.map.half.x + margin, w.map.half.x - margin);
    p.z = std::clamp(p.z, -w.map.half.z + margin, w.map.half.z - margin);
    return p;
}

// The light's reach between the keeper and a point: the fog at either end
// shortens it, a flare lengthens it.
float reachTo(const World& w, Vec2 p, bool withFlare) {
    float r = kLanternRadius;
    if (inFog(w, w.player) || inFog(w, p)) r *= kFogFactor;
    if (withFlare && w.flare > 0) r *= 1.6f;
    return r;
}

bool wispActive(const Wisp& s) { return s.dispelled <= 0; }

// Moves v towards `target` at up to `rate` per second.
Vec2 approach(Vec2 v, Vec2 target, float rate, float dt) {
    const Vec2 d = target - v;
    const float l = length(d), step = rate * dt;
    return l <= step ? target : v + d * (step / l);
}

void doFlare(World& w) {
    w.flare = kFlareTime;
    w.cooldown = kFlareCooldown;
    ++w.events.flares;
    const float far = (inFog(w, w.player) ? kFogFactor : 1.0f) * kLanternRadius * kFlareReach;
    for (size_t i = 0; i < w.wisps.size(); ++i) {
        Wisp& s = w.wisps[i];
        if (!wispActive(s) || distance(s.pos, w.player) > far) continue;
        s.dispelled = kWispAway;
        // Its ghosts are free: the ones in reach come to the lantern.
        for (Ghost& g : w.ghosts) {
            if (g.state != GhostState::Lured || g.wisp != static_cast<int>(i)) continue;
            g.wisp = -1;
            g.lureGuard = 2.0f;
            if (distance(g.pos, w.player) <= far) {
                g.state = GhostState::Following;
                ++w.events.rescued;
            } else {
                g.state = GhostState::Idle;
                g.anchor = g.pos;
            }
        }
    }
    for (Ghost& g : w.ghosts) {
        if (distance(g.pos, w.player) > far) continue;
        if (g.state == GhostState::Lured) {
            g.state = GhostState::Following;
            g.wisp = -1;
            g.lureGuard = 2.0f;
            ++w.events.rescued;
        } else if (g.state == GhostState::Idle || g.state == GhostState::Scared) {
            g.state = GhostState::Following;
            ++w.events.joined;
        }
    }
}

}  // namespace

bool inFog(const World& w, Vec2 p) { return inAny(w.map.fog, p); }

std::vector<Circle> buildings(const Level& l) {
    if (l.homeKind == HomeKind::Cottage) return {{l.home + Vec2{0, -3.8f}, 2.4f}};
    return {{l.home + Vec2{-1.7f, -2.3f}, 0.55f}, {l.home + Vec2{1.7f, -2.3f}, 0.55f}};
}

float reach(const World& w) { return reachTo(w, w.player, true); }

int ghostsHome(const World& w) {
    int n = 0;
    for (const Ghost& g : w.ghosts) n += g.state == GhostState::Home || g.state == GhostState::GoingHome ? 1 : 0;
    return n;
}

int ghostsFollowing(const World& w) {
    int n = 0;
    for (const Ghost& g : w.ghosts) n += g.state == GhostState::Following ? 1 : 0;
    return n;
}

bool levelDone(const World& w) { return w.doneFor >= 0; }

float flareReady(const World& w) { return std::clamp(1.0f - w.cooldown / kFlareCooldown, 0.0f, 1.0f); }

World start(int index, const Events* keep) {
    const auto& all = levels();
    index = std::clamp(index, 0, static_cast<int>(all.size()) - 1);
    World w;
    w.level = index;
    w.map = all[static_cast<size_t>(index)];
    w.player = w.map.start;
    // The keeper walks round the cottage, or the churchyard gate's pillars.
    for (const Circle& c : buildings(w.map)) w.map.trees.push_back(c);
    w.rng ^= static_cast<uint64_t>(index + 1) * 0x100000001B3ull;
    if (keep) w.events = *keep;
    for (Vec2 p : w.map.ghosts) {
        Ghost g;
        g.pos = g.anchor = p;
        g.phase = rand01(w) * 6.2831853f;
        w.ghosts.push_back(g);
    }
    for (size_t i = 0; i < w.map.wisps.size(); ++i) {
        Wisp s;
        s.along = 0;
        s.pos = alongLoop(w.map.wisps[i], 0);
        w.wisps.push_back(s);
    }
    return w;
}

void step(World& w, const Input& in, float dt) {
    w.time += dt;
    w.flare = std::max(0.0f, w.flare - dt);
    w.cooldown = std::max(0.0f, w.cooldown - dt);
    if (in.flare && w.cooldown <= 0 && w.doneFor < 0) doFlare(w);

    // ---- the keeper
    Vec2 m = in.move;
    if (length(m) > 1) m = normalized(m);
    const float want = in.hurry && length(m) > 0.05f ? kHurrySpeed : kWalkSpeed * length(m);
    const Vec2 target = normalized(m) * want;
    w.vel = approach(w.vel, target, 24.0f, dt);
    Vec2 p = w.player + w.vel * dt;
    auto pushOut = [&](const std::vector<Circle>& cs) {
        for (const Circle& c : cs) {
            const Vec2 d = p - c.c;
            const float l = length(d), r = c.r + kKeeperRadius;
            if (l < r) p = c.c + (l > 1e-4f ? d * (1.0f / l) : Vec2{0, 1}) * r;
        }
    };
    pushOut(w.map.ponds);
    pushOut(w.map.trees);
    p = clampTo(w, p, 0.6f);
    w.player = p;
    const float speed = length(w.vel);
    if (speed > 0.2f) w.facing = normalized(approach(w.facing, normalized(w.vel), 8.0f, dt));
    w.fastFor = speed > kScareSpeed ? w.fastFor + dt : 0.0f;
    const bool scaring = w.fastFor >= kScareAfter;

    // ---- the wisps
    for (size_t i = 0; i < w.wisps.size(); ++i) {
        Wisp& s = w.wisps[i];
        if (s.dispelled > 0) {
            s.dispelled -= dt;
            if (s.dispelled <= 0) {
                s.dispelled = 0;
                s.along = 0;
            }
        } else {
            s.along += kWispSpeed * dt;
        }
        s.pos = alongLoop(w.map.wisps[i], s.along);
    }
    auto nearWisp = [&](Vec2 at) {
        for (size_t i = 0; i < w.wisps.size(); ++i)
            if (wispActive(w.wisps[i]) && distance(w.wisps[i].pos, at) < kWispLure) return static_cast<int>(i);
        return -1;
    };

    // ---- the ghosts
    bool scaredNow = false;
    const Vec2 behind = w.player - w.facing * 1.5f;
    const Vec2 door = w.map.home + Vec2{0, -2.2f};
    for (size_t i = 0; i < w.ghosts.size(); ++i) {
        Ghost& g = w.ghosts[i];
        g.phase += dt;
        g.lureGuard = std::max(0.0f, g.lureGuard - dt);
        const float d = distance(g.pos, w.player);
        const float r = reachTo(w, g.pos, true);
        switch (g.state) {
        case GhostState::Idle: {
            const Vec2 drift = g.anchor + Vec2{std::cos(g.phase * 0.5f) * 1.0f, std::sin(g.phase * 0.37f) * 0.8f};
            g.vel = approach(g.vel, normalized(drift - g.pos) * std::min(0.7f, distance(drift, g.pos)), 2.0f, dt);
            if (d < r) {
                if (scaring) {
                    g.state = GhostState::Scared;
                    g.timer = 1.6f;
                    scaredNow = true;
                } else {
                    g.state = GhostState::Following;
                    ++w.events.joined;
                }
            } else if (g.lureGuard <= 0) {
                const int s = nearWisp(g.pos);
                if (s >= 0) {
                    g.state = GhostState::Lured;
                    g.wisp = s;
                    ++w.events.lured;
                }
            }
            break;
        }
        case GhostState::Following: {
            Vec2 sep{};
            for (size_t j = 0; j < w.ghosts.size(); ++j) {
                const Ghost& o = w.ghosts[j];
                if (j == i || (o.state != GhostState::Following && o.state != GhostState::Lured)) continue;
                const Vec2 a = g.pos - o.pos;
                const float l = length(a);
                if (l < 1.0f && l > 1e-4f) sep = sep + a * ((1.0f - l) / l * 3.0f);
            }
            const Vec2 to = behind - g.pos;
            const float pull = std::min(3.9f, length(to) * 2.0f);
            g.vel = approach(g.vel, normalized(to) * pull + sep, 14.0f, dt);
            if (distance(g.pos, w.map.home) < w.map.homeRadius) {
                g.state = GhostState::GoingHome;
                g.timer = 0;
                ++w.events.home;
            } else if (scaring && d < r) {
                g.state = GhostState::Scared;
                g.timer = 1.6f;
                scaredNow = true;
            } else if (d > reachTo(w, g.pos, false) * kLeash) {
                g.state = GhostState::Idle;
                g.anchor = g.pos;
                ++w.events.lost;
            } else if (g.lureGuard <= 0) {
                const int s = nearWisp(g.pos);
                if (s >= 0) {
                    g.state = GhostState::Lured;
                    g.wisp = s;
                    ++w.events.lured;
                }
            }
            break;
        }
        case GhostState::Scared: {
            const Vec2 away = normalized(g.pos - w.player);
            g.vel = away * (kScareSpeed * (0.3f + 0.7f * g.timer / 1.6f));
            g.timer -= dt;
            if (g.timer <= 0) {
                g.state = GhostState::Idle;
                g.anchor = clampTo(w, g.pos, 1.0f);
            }
            break;
        }
        case GhostState::Lured: {
            const Wisp& s = w.wisps[static_cast<size_t>(g.wisp)];
            const Vec2 orbit = s.pos + Vec2{std::cos(g.phase * 1.3f + static_cast<float>(i)) * 0.9f,
                                            std::sin(g.phase * 1.3f + static_cast<float>(i)) * 0.9f};
            g.vel = approach(g.vel, normalized(orbit - g.pos) * std::min(2.6f, distance(orbit, g.pos) * 2.0f), 8.0f, dt);
            if (d < 1.3f) {
                g.state = GhostState::Following;
                g.wisp = -1;
                g.lureGuard = 3.0f;
                ++w.events.rescued;
            }
            break;
        }
        case GhostState::GoingHome: {
            g.timer += dt;
            g.vel = approach(g.vel, normalized(door - g.pos) * 1.6f, 6.0f, dt);
            if (g.timer >= kGoingHomeTime) g.state = GhostState::Home;
            break;
        }
        case GhostState::Home:
            g.vel = {};
            break;
        }
        if (g.state != GhostState::Home) {
            g.pos = g.pos + g.vel * dt;
            if (g.state != GhostState::GoingHome) g.pos = clampTo(w, g.pos, 0.8f);
        }
        const float rise = g.state == GhostState::GoingHome ? g.timer / kGoingHomeTime * 0.8f : 0.0f;
        g.height = 0.95f + 0.12f * std::sin(g.phase * 2.1f) + rise;
    }
    if (scaredNow) {
        ++w.events.scattered;
        ++w.scatters;
    }

    // ---- home
    if (w.doneFor >= 0) {
        w.doneFor += dt;
    } else if (!w.ghosts.empty() &&
               std::all_of(w.ghosts.begin(), w.ghosts.end(), [](const Ghost& g) { return g.state == GhostState::Home; })) {
        w.doneFor = 0;
        ++w.events.levelsDone;
    }
}

void sendAllHome(World& w) {
    for (Ghost& g : w.ghosts) {
        if (g.state == GhostState::Home) continue;
        if (g.state != GhostState::GoingHome) ++w.events.home;
        g.state = GhostState::Home;
        g.pos = w.map.home + Vec2{0, -2.2f};
        g.vel = {};
    }
}

}  // namespace gl
