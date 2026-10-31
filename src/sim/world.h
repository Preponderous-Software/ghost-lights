// Ghost Lights: the rules. A keeper with a lantern walks a foggy marsh at
// night; lost little ghosts near the lantern's light follow, hurrying scatters
// them, will-o'-the-wisps lure them away, and the lantern's flare calls them
// back. Lead every ghost home to finish a level.
//
// No engine and no GPU: the world steps at a fixed 1/60 s, so it is the same
// on every machine and the tests drive it directly. The marsh is the X-Z
// plane; -Z is north (up the screen), +X east.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gl {

struct Vec2 {
    float x = 0, z = 0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
inline Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.z * k}; }
float length(Vec2 v);
float distance(Vec2 a, Vec2 b);
Vec2 normalized(Vec2 v);  // (0, 0) stays (0, 0)

struct Circle {
    Vec2 c;
    float r = 1;
};

enum class HomeKind { Cottage, Churchyard };

// A wisp drifts round a closed loop of points.
struct WispPath {
    std::vector<Vec2> points;
};

// One level of the marsh, as data (src/sim/levels.cpp).
struct Level {
    std::string name;
    std::string hint;       // shown on the level's opening card
    std::string touchHint;  // the same, for a touch screen
    Vec2 half{18, 18};      // the marsh runs from -half to +half
    Vec2 start;             // where the keeper starts
    Vec2 home;              // the middle of home's doorstep: a ghost here goes in
    float homeRadius = 2.6f;
    HomeKind homeKind = HomeKind::Cottage;
    std::vector<Vec2> ghosts;
    std::vector<Circle> ponds;  // the keeper walks round; ghosts float over
    std::vector<Circle> trees;  // the keeper walks round
    std::vector<Circle> fog;    // fog banks: the lantern's light reaches half as far inside
    std::vector<WispPath> wisps;
};

const std::vector<Level>& levels();

enum class GhostState : uint8_t { Idle, Following, Scared, Lured, GoingHome, Home };

struct Ghost {
    Vec2 pos, vel;
    Vec2 anchor;        // an idle ghost drifts round here
    GhostState state = GhostState::Idle;
    float timer = 0;    // scared: time left; going home: time in the doorway
    float phase = 0;    // for the drift and the bob
    int wisp = -1;      // lured: which wisp
    float lureGuard = 0;  // seconds before a wisp may lure it again
    float height = 0.9f;  // its float above the ground (for drawing)
};

struct Wisp {
    float along = 0;     // distance travelled along the loop
    Vec2 pos;
    float dispelled = 0; // seconds until it comes back after a flare
};

// What the player asks for this step.
struct Input {
    Vec2 move;           // -1..1 each way, screen-relative (+x right, +z down the screen = south)
    bool hurry = false;  // run: fast, and scares every ghost in the light
    bool flare = false;  // the lantern flares (when it is ready)
};

// Running totals of things that happened, for sound and the page: the page
// keeps the last values it saw and plays a sound for each one that went up.
struct Events {
    int joined = 0, scattered = 0, lost = 0, lured = 0, rescued = 0, home = 0, flares = 0, levelsDone = 0;
};

// Tuning, in metres and seconds.
constexpr float kDt = 1.0f / 60.0f;
constexpr float kWalkSpeed = 3.0f;
constexpr float kHurrySpeed = 6.2f;
constexpr float kScareSpeed = 4.4f;     // moving faster than this, for kScareAfter, scares ghosts in the light
constexpr float kScareAfter = 0.25f;
constexpr float kLanternRadius = 4.2f;  // ghosts closer than this join
constexpr float kFogFactor = 0.5f;      // the light's reach in a fog bank
constexpr float kLeash = 1.7f;          // a follower further than kLeash x reach is lost
constexpr float kFlareCooldown = 6.0f;
constexpr float kFlareTime = 1.2f;
constexpr float kFlareReach = 2.0f;     // a flare reaches this many times the light
constexpr float kWispLure = 2.6f;       // a ghost this close to a wisp is lured away
constexpr float kWispSpeed = 1.5f;
constexpr float kWispAway = 9.0f;       // a flared wisp is gone this long
constexpr float kKeeperRadius = 0.35f;
constexpr float kGoingHomeTime = 1.4f;

struct World {
    int level = 0;
    Level map;           // a copy of levels()[level]
    Vec2 player, vel;
    Vec2 facing{0, -1};
    float fastFor = 0;   // how long the keeper has been moving fast
    float flare = 0;     // > 0 while flaring
    float cooldown = 0;  // > 0 until the next flare
    std::vector<Ghost> ghosts;
    std::vector<Wisp> wisps;
    float time = 0;      // seconds in this level
    float doneFor = -1;  // >= 0 once every ghost is home
    int scatters = 0;    // this level's scares, for the end card
    Events events;
    uint64_t rng = 0x9E3779B97F4A7C15ull;
};

// A world at the start of level `index` (0-based). Events carry over when
// `keep` is given.
World start(int index, const Events* keep = nullptr);
void step(World& w, const Input& in, float dt = kDt);

// How far the lantern's light reaches now (the fog and a flare change it).
float reach(const World& w);
bool inFog(const World& w, Vec2 p);
// What stands at home and blocks the keeper (start() adds it to map.trees).
std::vector<Circle> buildings(const Level& l);
int ghostsHome(const World& w);     // home or on the doorstep going in
int ghostsFollowing(const World& w);
bool levelDone(const World& w);     // every ghost home
float flareReady(const World& w);   // 0 just used .. 1 ready

// Test and page hook: every ghost goes straight home.
void sendAllHome(World& w);

}  // namespace gl
