#include "fakeplayer.hpp"
#include "fakeplayer_shared.hpp"
#include "render_dispatch.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef void (*Tessellator_begin_t)(void*, void*, int, int, int);
typedef void (*Tessellator_color_t)(void*, float, float, float, float);
typedef void (*Tessellator_vertex_t)(void*, float, float, float);
typedef void (*MeshHelpers_renderMeshImmediately_t)(void*, void*, void*, char*);

struct HashedString {
    uint64_t mStrHash; std::string mStr; mutable const HashedString* mLastMatch;
    HashedString() : mStrHash(0), mStr(), mLastMatch(nullptr) {}
    explicit HashedString(const char* str) : mLastMatch(nullptr) {
        mStr = str ? str : "";
        constexpr uint64_t kO = 0xCBF29CE484222325ULL, kP = 0x100000001B3ULL;
        uint64_t h = kO;
        for (char c : mStr) h = (uint64_t)(unsigned char)c ^ (kP * h);
        mStrHash = mStr.empty() ? 0 : h;
    }
};

struct MaterialPtr {
    void* d[2]{nullptr, nullptr};
    MaterialPtr() = default;
    MaterialPtr(const MaterialPtr&) = delete;
    MaterialPtr& operator=(const MaterialPtr&) = delete;
    MaterialPtr(MaterialPtr&& o) noexcept { d[0]=o.d[0]; d[1]=o.d[1]; o.d[0]=o.d[1]=nullptr; }
    MaterialPtr& operator=(MaterialPtr&& o) noexcept {
        if (this!=&o){ d[0]=o.d[0]; d[1]=o.d[1]; o.d[0]=o.d[1]=nullptr; } return *this;
    }
    explicit operator bool() const { return d[0] != nullptr; }
};

static uintptr_t resolveADRP(uint32_t* insns, size_t count, uint32_t reg) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t insn = insns[i];
        if ((insn & 0x9F000000) != 0x90000000 || (insn & 0x1F) != reg) continue;
        int64_t imm = ((((int64_t)(insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 3));
        imm = (imm << 43) >> 43;
        uintptr_t page = ((uintptr_t)&insns[i] & ~0xFFFULL) + (uintptr_t)(imm << 12);
        for (size_t j = i + 1; j < count && j < i + 8; ++j) {
            uint32_t add = insns[j];
            if ((add & 0xFF000000) == 0x91000000 && ((add >> 5) & 0x1F) == reg && (add & 0x1F) == reg) {
                uint32_t imm12 = (add >> 10) & 0xFFF;
                if (add & 0x400000) imm12 <<= 12;
                return page + imm12;
            }
            if ((add & 0x1F) == reg) break;
        }
    }
    return 0;
}

static FakePlayerModule* g_mod = nullptr;
static Tessellator_begin_t s_tb = nullptr;
static Tessellator_color_t s_tc = nullptr;
static Tessellator_vertex_t s_tv = nullptr;
static MeshHelpers_renderMeshImmediately_t s_mesh = nullptr;
static MaterialPtr s_mat;
static uintptr_t s_matGroup = 0;
static bedrocktools::sdk::Vec3 g_pos{};
static bedrocktools::sdk::Vec2 g_rot{};
static void* g_lp = nullptr;
static float g_phase = 0.f;

using Line = std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>;
using Lines = std::vector<Line>;

static MaterialPtr getMat(const char* name) {
    if (!s_matGroup) return {};
    HashedString hs(name);
    void** vt = *reinterpret_cast<void***>(s_matGroup);
    if (!vt || !vt[2]) return {};
    using Fn = MaterialPtr(*)(void*, const HashedString*);
    return reinterpret_cast<Fn>(vt[2])((void*)s_matGroup, &hs);
}

static void drawLines(void* tess, void* sc, void* mat, float cx, float cy, float cz,
                      const Lines& lines, uint32_t color) {
    if (lines.empty() || !s_tb) return;
    float r = ((color >> 16) & 0xFF) / 255.f;
    float g = ((color >>  8) & 0xFF) / 255.f;
    float b = ((color      ) & 0xFF) / 255.f;
    float a = ((color >> 24) & 0xFF) / 255.f;
    s_tb(tess, nullptr, 4, (int)lines.size() * 2, 0);
    s_tc(tess, r, g, b, a);
    for (auto& ln : lines) {
        auto p1 = ln.first, p2 = ln.second;
        p1.x-=cx; p1.y-=cy; p1.z-=cz; p2.x-=cx; p2.y-=cy; p2.z-=cz;
        s_tv(tess, p1.x, p1.y, p1.z); s_tv(tess, p2.x, p2.y, p2.z);
    }
    char pad[64]{};
    s_mesh(sc, tess, mat, pad);
}

static void buildBody(Lines& out, const fakeplayer::Instance& fp, float scale) {
    float yaw = fp.yaw * (float)(M_PI / 180.0);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    auto rot = [&](float lx, float ly, float lz) -> bedrocktools::sdk::Vec3 {
        lx *= scale; ly *= scale; lz *= scale;
        return {fp.feet.x + lx*cy - lz*sy, fp.feet.y + ly, fp.feet.z + lx*sy + lz*cy};
    };
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        bedrocktools::sdk::Vec3 c[8] = {
            rot(x0,y0,z0), rot(x1,y0,z0), rot(x1,y0,z1), rot(x0,y0,z1),
            rot(x0,y1,z0), rot(x1,y1,z0), rot(x1,y1,z1), rot(x0,y1,z1)};
        int e[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for (auto& ed : e) out.push_back({c[ed[0]], c[ed[1]]});
    };
    // more detailed mannequin
    box(-0.28f, 0.00f, -0.10f, -0.02f, 0.72f, 0.10f); // L leg
    box( 0.02f, 0.00f, -0.10f,  0.28f, 0.72f, 0.10f); // R leg
    box(-0.32f, 0.72f, -0.14f,  0.32f, 1.38f, 0.14f); // torso
    box(-0.50f, 0.72f, -0.10f, -0.32f, 1.32f, 0.10f); // L arm
    box( 0.32f, 0.72f, -0.10f,  0.50f, 1.32f, 0.10f); // R arm
    box(-0.24f, 1.38f, -0.24f,  0.24f, 1.82f, 0.24f); // head
    // face indicator (nose)
    box(-0.04f, 1.52f, 0.24f, 0.04f, 1.60f, 0.32f);
}

static float lookDot() {
    auto& fp = fakeplayer::get();
    if (!fp.active) return -1.f;
    float cx = fp.feet.x, cy = fp.feet.y + fp.height * 0.55f, cz = fp.feet.z;
    float dx = cx - g_pos.x, dy = cy - g_pos.y, dz = cz - g_pos.z;
    float len = std::sqrt(dx*dx+dy*dy+dz*dz);
    if (len < 0.01f) return 1.f;
    dx/=len; dy/=len; dz/=len;
    float yaw = g_rot.y * (float)(M_PI/180.0);
    float pitch = g_rot.x * (float)(M_PI/180.0);
    float fx = -std::sin(yaw)*std::cos(pitch);
    float fy = -std::sin(pitch);
    float fz =  std::cos(yaw)*std::cos(pitch);
    return fx*dx + fy*dy + fz*dz;
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
    if (!g_mod->enabled) { fp.active = false; return; }

    if (g_mod->m_wantSpawn || !fp.active) {
        if (g_lp) {
            g_mod->spawnInFront();
            g_mod->m_wantSpawn = false;
        }
    }
    if (!fp.active) return;

    fp.lifeTime += 0.016f;
    g_phase += 0.016f;

    // zero knockback: position stays exactly where spawned
    if (g_mod->faceYou) {
        float dx = g_pos.x - fp.feet.x;
        float dz = g_pos.z - fp.feet.z;
        fp.yaw = std::atan2(dx, dz) * (float)(180.0 / M_PI);
    }

    // aim + close range → hit feedback (practice)
    float dx = g_pos.x - fp.feet.x, dz = g_pos.z - fp.feet.z;
    float d2 = dx*dx + dz*dz;
    float dot = lookDot();
    if (d2 < 20.f && dot > 0.94f) {
        float prev = fp.hitFlash;
        fp.hitFlash = std::min(1.f, fp.hitFlash + 0.12f);
        if (prev < 0.3f && fp.hitFlash >= 0.3f) fp.hitCount++;
    } else {
        fp.hitFlash = std::max(0.f, fp.hitFlash - 0.05f);
    }
}

static void onRender(void* levelRenderer, void* screenContext, void* /*a3*/) {
    if (!g_mod || !g_mod->enabled || !g_mod->drawBody) return;
    auto& fp = fakeplayer::get();
    if (!fp.active) return;
    if (!s_tb || !s_tc || !s_tv || !s_mesh) return;
    if (!screenContext || (uintptr_t)screenContext < 0x1000) return;

    uintptr_t tessPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mTessellator);
    if (!tessPtr || tessPtr < 0x1000) return;
    void* tess = (void*)tessPtr;

    uintptr_t lrp = *(uintptr_t*)((uintptr_t)levelRenderer + bedrocktools::sdk::offsets::LevelRenderer::mLevelRendererPlayer);
    if (!lrp || lrp < 0x1000) return;
    float camX = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos);
    float camY = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 4);
    float camZ = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 8);

    if (!s_mat && s_matGroup) s_mat = getMat("selection_box");
    void* mat = s_mat ? (void*)&s_mat
        : (void*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mSelectionOverlayMaterial);

    uintptr_t chPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mColorHolder);
    if (!chPtr || chPtr < 0x1000) return;
    float* ch = (float*)chPtr;
    float saved[4] = {ch[0],ch[1],ch[2],ch[3]};
    ch[0]=ch[1]=ch[2]=ch[3]=1.f;

    Lines body; body.reserve(80);
    buildBody(body, fp, std::clamp(g_mod->bodyScale, 0.5f, 2.0f));

    // color: idle cyan-white, flash red when aimed
    float t = fp.hitFlash;
    uint32_t r = (uint32_t)(200 + 55 * t);
    uint32_t g = (uint32_t)(220 * (1.f - t * 0.85f));
    uint32_t b = (uint32_t)(230 * (1.f - t * 0.7f));
    uint32_t col = 0xFF000000 | (r << 16) | (g << 8) | b;
    drawLines(tess, screenContext, mat, camX, camY, camZ, body, col);

    // outer AABB outline (hitbox preview)
    Lines box;
    float minx,miny,minz,maxx,maxy,maxz;
    fp.aabb(minx,miny,minz,maxx,maxy,maxz);
    bedrocktools::sdk::Vec3 p[8]={
        {minx,miny,minz},{maxx,miny,minz},{maxx,miny,maxz},{minx,miny,maxz},
        {minx,maxy,minz},{maxx,maxy,minz},{maxx,maxy,maxz},{minx,maxy,maxz}};
    int e[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto& ed : e) box.push_back({p[ed[0]], p[ed[1]]});
    drawLines(tess, screenContext, mat, camX, camY, camZ, box, t > 0.2f ? 0xFFFF4444 : 0x8844AAFF);

    // ground ring + soft pulse
    Lines ring;
    float pr = 0.4f + 0.08f * std::sin(g_phase * 3.f);
    for (int i = 0; i < 40; ++i) {
        float a0 = (float)(2*M_PI*i/40), a1 = (float)(2*M_PI*(i+1)/40);
        ring.push_back({
            {fp.feet.x+std::cos(a0)*pr, fp.feet.y+0.02f, fp.feet.z+std::sin(a0)*pr},
            {fp.feet.x+std::cos(a1)*pr, fp.feet.y+0.02f, fp.feet.z+std::sin(a1)*pr}});
    }
    drawLines(tess, screenContext, mat, camX, camY, camZ, ring, 0xFF55CCFF);

    ch[0]=saved[0]; ch[1]=saved[1]; ch[2]=saved[2]; ch[3]=saved[3];
}

void FakePlayerModule::spawnInFront() {
    if (!g_lp) return;
    float yaw = g_rot.y * (float)(M_PI / 180.0);
    float fx = -std::sin(yaw), fz = std::cos(yaw);
    auto& fp = fakeplayer::get();
    fp.feet = {g_pos.x + fx * distance, g_pos.y + heightOffset, g_pos.z + fz * distance};
    fp.yaw = g_rot.y + 180.f;
    fp.height = 1.8f * std::clamp(bodyScale, 0.5f, 2.0f);
    fp.width = 0.6f * std::clamp(bodyScale, 0.5f, 2.0f);
    fp.hitFlash = 0.f;
    fp.lifeTime = 0.f;
    fp.hitCount = 0;
    fakeplayer::setName(dummyName.c_str());
    fp.active = true;
}

FakePlayerModule::FakePlayerModule()
    : Module("FakePlayer", "Practice dummy in front of you — zero knockback, TargetESP target.") {
    showInMenu = true;
    g_mod = this;
}
FakePlayerModule::~FakePlayerModule() {
    if (g_mod == this) g_mod = nullptr;
    fakeplayer::get().active = false;
}

void FakePlayerModule::onInit() {
    if (auto tb = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorBegin))
        s_tb = (Tessellator_begin_t)tb;
    if (auto tc = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorColor))
        s_tc = (Tessellator_color_t)tc;
    if (auto tv = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorVertex))
        s_tv = (Tessellator_vertex_t)tv;
    if (auto rm = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately2))
        s_mesh = (MeshHelpers_renderMeshImmediately_t)rm;
    else if (auto rm5 = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately))
        s_mesh = (MeshHelpers_renderMeshImmediately_t)rm5;
    if (auto rmg = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderMaterialGroupCommon)) {
        uintptr_t g = resolveADRP(reinterpret_cast<uint32_t*>(rmg), 2, 0);
        if (g) s_matGroup = g + bedrocktools::sdk::offsets::MaterialGroup::mRenderMaterialGroupOffset;
    }
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& e) { onTick(e.player); });
    render_dispatch::add(onRender);
}

void FakePlayerModule::onEnable() {
    render_dispatch::ensureHooked();
    m_wantSpawn = true; // respawn each enable
}

void FakePlayerModule::onDisable() {
    fakeplayer::get().active = false;
    m_wantSpawn = false;
}

void FakePlayerModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    distance = j.value("distance", distance);
    heightOffset = j.value("heightOffset", heightOffset);
    faceYou = j.value("faceYou", faceYou);
    drawBody = j.value("drawBody", drawBody);
    drawName = j.value("drawName", drawName);
    lockPosition = j.value("lockPosition", lockPosition);
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
    j["drawName"] = drawName;
    j["lockPosition"] = lockPosition;
    j["bodyScale"] = bodyScale;
    j["dummyName"] = dummyName;
}
