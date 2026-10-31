// Ghost Lights on Strata: the marsh drawn from the rules (src/sim), the
// camera, and the player's input. One Game per page.
#pragma once

#include <cstdint>
#include <vector>

#include "sim/world.h"
#include "strata/strata.hpp"

namespace gl::play {

class Game {
public:
    explicit Game(strata::Engine& engine);

    // Keys or the touch stick: screen-relative, -1..1 each way.
    void setMove(float x, float z, bool hurry);
    // A held mouse button: walk towards the ground under this point of the
    // canvas (0..1 across, 0..1 down: no pixel ratios to get wrong).
    void setPointer(float px, float py, bool down, bool hurry);
    void flare() { flareAsked_ = true; }
    void next();
    void restart();
    void goToLevel(int index);
    void solve() { sendAllHome(world_); }
    void setPaused(bool paused) { paused_ = paused; }
    void setAutopilot(bool on) { autopilot_ = on; }
    bool finished() const { return finished_; }

    // Advances the world by `seconds` of real time (in fixed steps) and draws a frame.
    void frame(double seconds);
    const World& world() const { return world_; }
    bool hurrying() const { return world_.fastFor > 0; }

private:
    void build();
    void camera(float aspect);
    void draw();

    strata::Engine& engine_;
    World world_;
    Input keys_;
    bool pointerDown_ = false, pointerHurry_ = false;
    float pointerX_ = 0, pointerY_ = 0;
    bool flareAsked_ = false, paused_ = false, autopilot_ = false, finished_ = false;
    double carry_ = 0;
    float clock_ = 0;  // seconds drawn, for animation

    // The camera, as last set.
    float eye_[3] = {0, 0, 0}, look_[3] = {0, 0, 0};
    float fov_ = 45.0f, aspect_ = 1.0f;
    bool cameraSet_ = false;

    std::vector<uint32_t> ghostMeshes_;
    std::vector<uint32_t> levelMeshes_;
    std::vector<StrataRenderItem> statics_;  // reeds' heads, pumpkins, stones: primitives drawn every frame
    struct Puff {
        float x, y, z, size, alpha, drift;
    };
    std::vector<Puff> fog_;
    std::vector<Vec2> windows_;  // where home ghosts peek out
    float windowY_ = 1.2f;
};

}  // namespace gl::play
