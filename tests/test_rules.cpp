// The rules of the marsh: following, scattering, fog, wisps, the flare, home,
// and that the autopilot can finish every level without one scare.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "sim/autopilot.h"
#include "sim/world.h"

using namespace gl;

namespace {

int failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

// An empty test marsh: one ghost, nothing else.
World blank(Vec2 ghost) {
    World w = start(0);
    w.map.ponds.clear();
    w.map.trees.clear();
    w.map.fog.clear();
    w.map.wisps.clear();
    w.map.half = {30, 30};
    w.map.home = {0, -25};
    w.wisps.clear();
    w.player = {0, 0};
    w.vel = {};
    w.ghosts.clear();
    Ghost g;
    g.pos = g.anchor = ghost;
    w.ghosts.push_back(g);
    return w;
}

void run(World& w, Input in, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) step(w, in);
}

void testFollow() {
    World w = blank({2, 0});
    run(w, {}, 0.1f);
    CHECK(w.ghosts[0].state == GhostState::Following);
    CHECK(w.events.joined == 1);
    // Walk north for four seconds: the ghost keeps up.
    Input in;
    in.move = {0, -1};
    run(w, in, 4.0f);
    CHECK(w.player.z < -10.0f);
    CHECK(w.ghosts[0].state == GhostState::Following);
    CHECK(distance(w.ghosts[0].pos, w.player) < 3.0f);

    World far = blank({9, 0});
    run(far, {}, 1.0f);
    CHECK(far.ghosts[0].state == GhostState::Idle);
}

void testScatter() {
    World w = blank({2, 0});
    run(w, {}, 0.1f);
    CHECK(w.ghosts[0].state == GhostState::Following);
    Input in;
    in.move = {0, -1};
    in.hurry = true;
    run(w, in, 0.6f);
    CHECK(w.ghosts[0].state == GhostState::Scared);
    CHECK(w.events.scattered == 1);
    CHECK(w.scatters == 1);
    run(w, {}, 3.0f);
    CHECK(w.ghosts[0].state != GhostState::Scared);
    // A short dash (under kScareAfter) scares nobody.
    World v = blank({2, 0});
    run(v, {}, 0.1f);
    run(v, in, 0.1f);
    run(v, {}, 0.5f);
    CHECK(v.events.scattered == 0);
}

void testFog() {
    World w = blank({3, 0});
    w.map.fog.push_back({{0, 0}, 6});
    run(w, {}, 0.5f);
    CHECK(w.ghosts[0].state == GhostState::Idle);  // 3 m is out of reach in fog
    w.ghosts[0].pos = w.ghosts[0].anchor = {1.5f, 0};
    run(w, {}, 0.2f);
    CHECK(w.ghosts[0].state == GhostState::Following);
}

void testWispAndFlare() {
    World w = blank({2, 0});
    run(w, {}, 0.1f);
    CHECK(w.ghosts[0].state == GhostState::Following);
    // A wisp sits right on the ghost.
    WispPath p;
    p.points = {{3, 0}, {3.1f, 0}};
    w.map.wisps.push_back(p);
    w.wisps.push_back(Wisp{});
    run(w, {}, 0.2f);
    CHECK(w.ghosts[0].state == GhostState::Lured);
    CHECK(w.events.lured == 1);
    Input f;
    f.flare = true;
    step(w, f);
    CHECK(w.ghosts[0].state == GhostState::Following);
    CHECK(w.events.flares == 1);
    CHECK(w.events.rescued == 1);
    CHECK(w.wisps[0].dispelled > 0);
    CHECK(flareReady(w) < 0.1f);
    // A second flare straight away does nothing.
    step(w, f);
    CHECK(w.events.flares == 1);
    run(w, {}, kFlareCooldown);
    CHECK(flareReady(w) >= 1.0f);
}

void testFlareCalls() {
    World w = blank({7, 0});  // out of the lantern's reach, in the flare's
    run(w, {}, 0.1f);
    CHECK(w.ghosts[0].state == GhostState::Idle);
    Input f;
    f.flare = true;
    step(w, f);
    CHECK(w.ghosts[0].state == GhostState::Following);
}

void testHome() {
    World w = blank({2, 0});
    w.map.home = {0, -6};
    run(w, {}, 0.1f);
    Input in;
    in.move = {0, -1};
    run(w, in, 4.0f);
    run(w, {}, 3.0f);
    CHECK(w.ghosts[0].state == GhostState::Home);
    CHECK(ghostsHome(w) == 1);
    CHECK(levelDone(w));
    CHECK(w.events.levelsDone == 1);
}

void testPonds() {
    World w = blank({20, 20});
    w.map.ponds.push_back({{0, -4}, 2});
    Input in;
    in.move = {0, -1};
    run(w, in, 3.0f);
    CHECK(distance(w.player, {0, -4}) >= 2.0f + kKeeperRadius - 1e-3f);
    // The edge of the marsh.
    run(w, in, 30.0f);
    CHECK(w.player.z >= -w.map.half.z);
}

void testLevels() {
    CHECK(levels().size() == 5);
    Events e;
    for (int i = 0; i < static_cast<int>(levels().size()); ++i) {
        World w = start(i, &e);
        const bool ok = playLevel(w, 300.0f);
        std::printf("level %d \"%s\": %s in %.1f s, %d scares, %d lured, %d flares\n", i + 1, w.map.name.c_str(),
                    ok ? "done" : "NOT DONE", static_cast<double>(w.time), w.scatters, w.events.lured - e.lured,
                    w.events.flares - e.flares);
        CHECK(ok);
        CHECK(w.scatters == 0);
        CHECK(ghostsHome(w) == static_cast<int>(w.ghosts.size()));
        e = w.events;
    }
    CHECK(e.levelsDone == 5);
    // The same inputs give the same world.
    World a = start(4), b = start(4);
    playLevel(a, 30.0f);
    playLevel(b, 30.0f);
    CHECK(a.player.x == b.player.x && a.player.z == b.player.z && a.events.joined == b.events.joined);
}

void testSendHome() {
    World w = start(2);
    sendAllHome(w);
    step(w, {});
    CHECK(levelDone(w));
}

}  // namespace

int main() {
    testFollow();
    testScatter();
    testFog();
    testWispAndFlare();
    testFlareCalls();
    testHome();
    testPonds();
    testLevels();
    testSendHome();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all rules hold\n");
    return 0;
}
