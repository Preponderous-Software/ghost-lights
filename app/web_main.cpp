// Ghost Lights in a browser: the game on Strata's WebGL 2 backend
// (Emscripten). The page (web/shell.html) owns the controls, the cards and
// the sound; it calls the gl_* functions below and reads gl_status() a few
// times a second.
#include <cstdio>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "play/scene.h"
#include "sim/world.h"
#include "strata/strata.hpp"

using namespace gl;

namespace {

struct App {
    std::unique_ptr<strata::Engine> engine;
    std::unique_ptr<play::Game> game;
    double last = 0;
    std::string status;
};

App* app = nullptr;

void frame(void*) {
    StrataEvent e;
    while (app->engine->pollEvent(e)) {
    }
    const double now = emscripten_get_now() / 1000.0;
    app->game->frame(now - app->last);
    app->last = now;
    app->engine->present();
}

void jsonString(std::string& out, const std::string& s) {
    out += '"';
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (static_cast<unsigned char>(ch) < 0x20) {
            out += ' ';
        } else {
            out += ch;
        }
    }
    out += '"';
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void gl_move(double x, double z, int hurry) {
    if (app) app->game->setMove(static_cast<float>(x), static_cast<float>(z), hurry != 0);
}
// A held mouse button: where on the canvas, 0..1 across and down.
EMSCRIPTEN_KEEPALIVE void gl_pointer(double x, double y, int down, int hurry) {
    if (app) app->game->setPointer(static_cast<float>(x), static_cast<float>(y), down != 0, hurry != 0);
}
EMSCRIPTEN_KEEPALIVE void gl_flare() {
    if (app) app->game->flare();
}
EMSCRIPTEN_KEEPALIVE void gl_next() {
    if (app) app->game->next();
}
EMSCRIPTEN_KEEPALIVE void gl_restart() {
    if (app) app->game->restart();
}
EMSCRIPTEN_KEEPALIVE void gl_level(int index) {
    if (app) app->game->goToLevel(index);
}
// For the smoke test: every ghost goes home.
EMSCRIPTEN_KEEPALIVE void gl_solve() {
    if (app) app->game->solve();
}
EMSCRIPTEN_KEEPALIVE void gl_autopilot(int on) {
    if (app) app->game->setAutopilot(on != 0);
}
EMSCRIPTEN_KEEPALIVE void gl_resize(double w, double h, int paused) {
    if (!app) return;
    if (w >= 1 && h >= 1) app->engine->resize(static_cast<int32_t>(w), static_cast<int32_t>(h));
    app->game->setPaused(paused != 0);
}
EMSCRIPTEN_KEEPALIVE const char* gl_status() {
    if (!app) return "{}";
    const play::Game& g = *app->game;
    const World& w = g.world();
    const Events& e = w.events;
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"level\":%d,\"levels\":%d,\"home\":%d,\"ghosts\":%d,\"following\":%d,\"done\":%s,\"finished\":%s,"
                  "\"time\":%.2f,\"scatters\":%d,\"flare\":%.3f,\"hurrying\":%s,\"x\":%.3f,\"z\":%.3f,"
                  "\"ev\":{\"joined\":%d,\"scattered\":%d,\"lost\":%d,\"lured\":%d,\"rescued\":%d,\"home\":%d,\"flares\":%d,\"levelsDone\":%d}",
                  w.level, static_cast<int>(levels().size()), ghostsHome(w), static_cast<int>(w.ghosts.size()), ghostsFollowing(w),
                  levelDone(w) ? "true" : "false", g.finished() ? "true" : "false", static_cast<double>(w.time), w.scatters,
                  static_cast<double>(flareReady(w)), g.hurrying() ? "true" : "false", static_cast<double>(w.player.x),
                  static_cast<double>(w.player.z), e.joined, e.scattered, e.lost, e.lured, e.rescued, e.home, e.flares, e.levelsDone);
    std::string o = buf;
    o += ",\"name\":";
    jsonString(o, w.map.name);
    o += ",\"hint\":";
    jsonString(o, w.map.hint);
    o += ",\"touchHint\":";
    jsonString(o, w.map.touchHint);
    o += ",\"wisps\":" + std::to_string(w.wisps.size());
    o += ",\"fog\":" + std::to_string(w.map.fog.size());
    o += '}';
    app->status = std::move(o);
    return app->status.c_str();
}

}  // extern "C"

int main() {
    static App a;
    app = &a;
    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    if (cw < 1 || ch < 1) {
        cw = 1280;
        ch = 720;
    }
    strata::EngineDesc desc;
    desc.surface = STRATA_SURFACE_WINDOW;
    desc.width = static_cast<int32_t>(cw);
    desc.height = static_cast<int32_t>(ch);
    desc.title = "Ghost Lights";
    try {
        app->engine = strata::Engine::create(desc);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "ghost-lights: cannot start the renderer: %s\n", ex.what());
        std::string js = "if (Module.onRendererFailed) Module.onRendererFailed(";
        jsonString(js, ex.what());
        js += ")";
        emscripten_run_script(js.c_str());
        return 1;
    }
    app->game = std::make_unique<play::Game>(*app->engine);
    app->last = emscripten_get_now() / 1000.0;
    emscripten_run_script("if (Module.onGameReady) Module.onGameReady()");
    emscripten_set_main_loop_arg(frame, nullptr, 0, false);
    return 0;
}
