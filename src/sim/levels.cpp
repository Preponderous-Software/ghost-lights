// The five nights on the marsh, easiest first. Home is always to the north
// (up the screen), the keeper starts in the south.
#include "sim/world.h"

namespace gl {

namespace {

WispPath loop(Vec2 c, float rx, float rz) {
    // An eight-point loop round c.
    WispPath p;
    const float k = 0.7071f;
    const Vec2 dirs[8] = {{1, 0}, {k, k}, {0, 1}, {-k, k}, {-1, 0}, {-k, -k}, {0, -1}, {k, -k}};
    for (Vec2 d : dirs) p.points.push_back({c.x + d.x * rx, c.z + d.z * rz});
    return p;
}

std::vector<Level> make() {
    std::vector<Level> all;
    {
        Level l;
        l.name = "The Lantern";
        l.hint = "Little lost ghosts are drifting on the marsh. Walk close and they follow your lantern. "
                 "Lead them to the cottage door. (WASD or the arrow keys, or hold the mouse button where you want to go.)";
        l.touchHint = "Little lost ghosts are drifting on the marsh. Walk close and they follow your lantern. "
                      "Lead them to the cottage door. (Drag anywhere to walk.)";
        l.half = {12, 14};
        l.start = {0, 10};
        l.home = {0, -8.5f};
        l.ghosts = {{-3.5f, 4}, {4, 1}, {-2, -2.5f}};
        l.ponds = {{{6.5f, 6.5f}, 2.2f}};
        l.trees = {{{-7, -5}, 0.6f}, {{8, -3}, 0.6f}, {{-8, 8}, 0.6f}};
        all.push_back(l);
    }
    {
        Level l;
        l.name = "Softly Now";
        l.hint = "Walk softly. Hurrying (Shift) frightens the ghosts in your light, and they scatter.";
        l.touchHint = "Walk softly. Pushing all the way out makes you hurry, and that frightens the ghosts in your light.";
        l.half = {16, 16};
        l.start = {0, 13};
        l.home = {0, -11};
        l.ghosts = {{-9, 8}, {9.5f, 9}, {-7, -1}, {7.5f, -3}, {0.5f, 2.5f}};
        l.ponds = {{{-3, 8}, 2.3f}, {{4.5f, 4.5f}, 2.0f}, {{-9.5f, 3}, 1.7f}, {{3, -5.5f}, 2.1f}};
        l.trees = {{{-12, -6}, 0.6f}, {{12, -8}, 0.6f}, {{11, 2}, 0.6f}, {{-4, -8}, 0.6f}};
        all.push_back(l);
    }
    {
        Level l;
        l.name = "Fog on the Fen";
        l.hint = "Fog swallows the light. In a fog bank your lantern reaches only half as far, so walk close.";
        l.touchHint = l.hint;
        l.half = {16, 18};
        l.start = {0, 15};
        l.home = {2, -13};
        l.ghosts = {{-10, 9}, {10, 6}, {-5, 0}, {8, -4}, {-9, -8}, {3.5f, 9}};
        l.fog = {{{-8.5f, 4.5f}, 5.0f}, {{7.5f, -2.5f}, 5.0f}, {{-3, -9}, 4.5f}};
        l.ponds = {{{0, 3.5f}, 2.2f}, {{-2.5f, -3.5f}, 1.6f}, {{11.5f, 11}, 2.0f}};
        l.trees = {{{-13, -13}, 0.6f}, {{13, -12}, 0.6f}, {{-12, 14}, 0.6f}, {{5, 13}, 0.6f}};
        all.push_back(l);
    }
    {
        Level l;
        l.name = "Will-o'-the-Wisps";
        l.hint = "Will-o'-the-wisps lure ghosts away. Press Space to flare your lantern: "
                 "the flare chases wisps off and calls ghosts back to you.";
        l.touchHint = "Will-o'-the-wisps lure ghosts away. Tap Flare: the flare chases wisps off and calls ghosts back to you.";
        l.half = {17, 18};
        l.start = {0, 15};
        l.home = {0, -13};
        l.ghosts = {{-11, 10}, {11, 9}, {-6.5f, 1.5f}, {9.5f, 0}, {-10.5f, -6}, {6.5f, -8}};
        l.ponds = {{{-2.5f, 9}, 2.0f}, {{12, -12}, 2.2f}, {{-12, -12}, 2.0f}};
        l.trees = {{{14, 14}, 0.6f}, {{-14, 3}, 0.6f}, {{3, -2}, 0.6f}};
        l.wisps = {loop({-3, 4}, 4.0f, 2.8f), loop({4.5f, -5}, 3.5f, 3.0f)};
        all.push_back(l);
    }
    {
        Level l;
        l.name = "All Hallows' Eve";
        l.hint = "The churchyard gate stands open tonight. Bring every last ghost home, through the fog and past the wisps.";
        l.touchHint = l.hint;
        l.half = {19, 20};
        l.start = {0, 17};
        l.home = {0, -14.5f};
        l.homeKind = HomeKind::Churchyard;
        l.ghosts = {{-13, 13}, {13, 12}, {-6, 7}, {7, 5}, {-14, 0}, {14, -2},
                    {-7, -8}, {8, -9}, {0.5f, 0}};
        l.fog = {{{-11, 9}, 5.0f}, {{11, -3}, 5.0f}, {{-4, -9}, 4.0f}};
        l.ponds = {{{4, 11}, 2.3f}, {{-3.5f, 3}, 2.0f}, {{6, -1.5f}, 1.8f}, {{-13, -10}, 2.2f}};
        l.trees = {{{-17, 17}, 0.6f}, {{17, 17}, 0.6f}, {{16, -12}, 0.6f}, {{-9, 15}, 0.6f}, {{11, 8}, 0.6f}};
        l.wisps = {loop({-8, 3}, 3.5f, 3.0f), loop({4, -6}, 3.5f, 2.5f), loop({11, 13}, 3.0f, 2.5f)};
        all.push_back(l);
    }
    return all;
}

}  // namespace

const std::vector<Level>& levels() {
    static const std::vector<Level> all = make();
    return all;
}

}  // namespace gl
