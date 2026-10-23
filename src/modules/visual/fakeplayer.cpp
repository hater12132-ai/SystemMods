#include "fakeplayer.hpp"
#include "fakeplayer_shared.hpp"
#include "render_dispatch.hpp"
#include "tess_util.hpp"
#include "sm_status.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <cmath>
#include <algorithm>

static FakePlayerModule* g_mod = nullptr;
static bedrocktools::sdk::Vec3 g_pos{};
static bedrocktools::sdk::Vec2 g_rot{};
static void* g_lp = nullptr;
static int g_renderFrames = 0;

void FakePlayerModule::spawnAtPlayer() {
    if (!g_lp) {
        sm_status::write("FakePlayer: spawn skipped — no local player yet");
        return;
    }
    float yaw = g_rot.y * (float)(M_PI / 180.0);
    float fx = -std::sin(yaw), fz = std::cos(yaw);
    auto& fp = fakeplayer::get();
    float sc = std::clamp(bodyScale, 0.5f, 2.5f);
    // distance 0 = exact copy of your feet position
    fp.feet = {
        g_pos.x + fx * distance,
        g_pos.y + heightOffset,
        g_pos.z + fz * distance
    };
    fp.yaw = g_rot.y; // same facing as you (copy)
    fp.height = 1.8f * sc;
    fp.width = 0.6f * sc;
    fp.hitFlash = 0.f;
    fakeplayer::setName(dummyName.c_str());
    fp.active = true;
    sm_status::write("FakePlayer: COPY at %.2f %.2f %.2f yaw=%.1f dist=%.1f follow=%d",
        fp.feet.x, fp.feet.y, fp.feet.z, fp.yaw, distance, followPlayer ? 1 : 0);
}

static void onTick(void* player) {
    if (!g_mod) return;
    g_lp = player;
    uintptr_t a = (uintptr_t)player;
    uintptr_t svc = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (svc) g_pos = *(bedrocktools::sdk::Vec3*)svc;
    uintptr_t rot = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mActorRotationComponent);
    if (rot) g_rot = *(bedrocktools::sdk::Vec2*)rot;

    auto& fp = fakeplayer::get();
    if (!g_mod->enabled) {
        fp.active = false;
        return;
    }
    if (g_mod->wantSpawn || !fp.active) {
        g_mod->spawnAtPlayer();
        g_mod->wantSpawn = false;
    }
    if (!fp.active) return;

    // Moving copy of yourself
    if (g_mod->followPlayer) {
        float yaw = g_rot.y * (float)(M_PI / 180.0);
        float fx = -std::sin(yaw), fz = std::cos(yaw);
        fp.feet = {
            g_pos.x + fx * g_mod->distance,
            g_pos.y + g_mod->heightOffset,
            g_pos.z + fz * g_mod->distance
        };
        fp.yaw = g_rot.y;
    } else if (g_mod->faceYou) {
        float dx = g_pos.x - fp.feet.x;
        float dz = g_pos.z - fp.feet.z;
        fp.yaw = std::atan2(dx, dz) * (float)(180.0 / M_PI);
    }
    // else: locked world position = zero knockback stationary copy
}

static void onRender(void* levelRenderer, void* screenContext, void* /*a3*/) {
    if (!g_mod || !g_mod->enabled) return;
    auto& fp = fakeplayer::get();
    if (!fp.active || !g_mod->drawBody) return;
    if (!screenContext || (uintptr_t)screenContext < 0x1000) return;

    auto& T = tess_util::ctx();
    if (!T.ready()) return; // keeps retrying inside ready()

    uintptr_t tessPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mTessellator);
    if (!tessPtr || tessPtr < 0x1000) return;
    void* tess = (void*)tessPtr;

    uintptr_t lrp = *(uintptr_t*)((uintptr_t)levelRenderer + bedrocktools::sdk::offsets::LevelRenderer::mLevelRendererPlayer);
    if (!lrp || lrp < 0x1000) return;
    float camX = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos);
    float camY = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 4);
    float camZ = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 8);

    T.ensureMat();
    void* mat = T.matSelection
        ? (void*)&T.matSelection
        : (void*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mSelectionOverlayMaterial);

    uintptr_t chPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mColorHolder);
    if (!chPtr || chPtr < 0x1000) return;
    float* ch = (float*)chPtr;
    float saved[4] = {ch[0], ch[1], ch[2], ch[3]};
    ch[0] = ch[1] = ch[2] = ch[3] = 1.f;

    float hw = fp.width * 0.5f;
    float minx = fp.feet.x - hw, maxx = fp.feet.x + hw;
    float miny = fp.feet.y, maxy = fp.feet.y + fp.height;
    float minz = fp.feet.z - hw, maxz = fp.feet.z + hw;

    tess_util::Lines lines;
    lines.reserve(64);
    tess_util::Context::addBox(lines, minx, miny, minz, maxx, maxy, maxz);
    // head
    float hy0 = fp.feet.y + fp.height * 0.75f;
    tess_util::Context::addBox(lines, minx + hw * 0.25f, hy0, minz + hw * 0.25f,
                                      maxx - hw * 0.25f, maxy, maxz - hw * 0.25f);
    // bright ground ring
    tess_util::Context::addRing(lines, fp.feet.x, fp.feet.z, fp.feet.y + 0.05f, 0.6f, 0.6f, 40, 0.f);
    // top cross
    float top = maxy + 0.2f;
    lines.push_back({{fp.feet.x - 0.35f, top, fp.feet.z}, {fp.feet.x + 0.35f, top, fp.feet.z}});
    lines.push_back({{fp.feet.x, top, fp.feet.z - 0.35f}, {fp.feet.x, top, fp.feet.z + 0.35f}});

    T.drawLines(tess, screenContext, mat, camX, camY, camZ, lines, 0xFF00FFFF);
    tess_util::Lines thick;
    tess_util::Context::addBox(thick, minx - 0.03f, miny, minz - 0.03f, maxx + 0.03f, maxy, maxz + 0.03f);
    T.drawLines(tess, screenContext, mat, camX, camY, camZ, thick, 0xCCFFFFFF);

    ch[0] = saved[0]; ch[1] = saved[1]; ch[2] = saved[2]; ch[3] = saved[3];

    if ((++g_renderFrames % 90) == 1) {
        sm_status::write("FakePlayer: DRAWING frame=%d at %.1f %.1f %.1f lines=%zu",
            g_renderFrames, fp.feet.x, fp.feet.y, fp.feet.z, lines.size());
    }
}

FakePlayerModule::FakePlayerModule()
    : Module("FakePlayer", "Copy of you at your coords (cyan box). Zero knockback when not following.") {
    showInMenu = true;
    g_mod = this;
}
FakePlayerModule::~FakePlayerModule() {
    if (g_mod == this) g_mod = nullptr;
    fakeplayer::get().active = false;
}

void FakePlayerModule::onInit() {
    sm_status::write("FakePlayer: onInit");
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& e) { onTick(e.player); });
    render_dispatch::add(onRender);
}

void FakePlayerModule::onEnable() {
    sm_status::write("FakePlayer: onEnable");
    render_dispatch::ensureHooked();
    wantSpawn = true;
}

void FakePlayerModule::onDisable() {
    sm_status::write("FakePlayer: onDisable");
    fakeplayer::get().active = false;
    wantSpawn = false;
}

void FakePlayerModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    distance = j.value("distance", distance);
    heightOffset = j.value("heightOffset", heightOffset);
    faceYou = j.value("faceYou", faceYou);
    drawBody = j.value("drawBody", drawBody);
    followPlayer = j.value("followPlayer", followPlayer);
    bodyScale = j.value("bodyScale", bodyScale);
    if (j.contains("dummyName") && j["dummyName"].is_string())
        dummyName = j["dummyName"].get<std::string>();
}

void FakePlayerModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["distance"] = distance;
    j["heightOffset"] = heightOffset;
    j["faceYou"] = faceYou;
    j["drawBody"] = drawBody;
    j["followPlayer"] = followPlayer;
    j["bodyScale"] = bodyScale;
    j["dummyName"] = dummyName;
}
