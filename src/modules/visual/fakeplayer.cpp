#include "fakeplayer.hpp"
#include "fakeplayer_shared.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include "core/memory/Hooks.hpp"
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
    void* sharedPtrData[2]{nullptr, nullptr};
    MaterialPtr() = default;
    MaterialPtr(const MaterialPtr&) = delete;
    MaterialPtr& operator=(const MaterialPtr&) = delete;
    MaterialPtr(MaterialPtr&& o) noexcept : sharedPtrData{o.sharedPtrData[0], o.sharedPtrData[1]} {
        o.sharedPtrData[0] = o.sharedPtrData[1] = nullptr;
    }
    MaterialPtr& operator=(MaterialPtr&& o) noexcept {
        if (this != &o) {
            sharedPtrData[0] = o.sharedPtrData[0]; sharedPtrData[1] = o.sharedPtrData[1];
            o.sharedPtrData[0] = o.sharedPtrData[1] = nullptr;
        }
        return *this;
    }
    ~MaterialPtr() {}
    explicit operator bool() const { return sharedPtrData[0] != nullptr; }
};

static uintptr_t resolveADRP(uint32_t* insns, size_t count, uint32_t targetReg) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t insn = insns[i];
        if ((insn & 0x9F000000) != 0x90000000) continue;
        if ((insn & 0x1F) != targetReg) continue;
        int64_t immhi = (insn >> 5) & 0x7FFFF;
        int64_t immlo = (insn >> 29) & 0x3;
        int64_t imm = ((immhi << 2) | immlo);
        imm = (imm << 43) >> 43;
        uintptr_t page = ((uintptr_t)&insns[i] & ~0xFFFULL) + (uintptr_t)(imm << 12);
        for (size_t j = i + 1; j < count && j < i + 8; ++j) {
            uint32_t add = insns[j];
            if ((add & 0xFF000000) == 0x91000000 &&
                ((add >> 5) & 0x1F) == targetReg && (add & 0x1F) == targetReg) {
                uint32_t imm12 = (add >> 10) & 0xFFF;
                if (add & 0x400000) imm12 <<= 12;
                return page + imm12;
            }
            if ((add & 0x1F) == targetReg) break;
        }
    }
    return 0;
}

static FakePlayerModule* g_mod = nullptr;
static Tessellator_begin_t s_tessBegin = nullptr;
static Tessellator_color_t s_tessColor = nullptr;
static Tessellator_vertex_t s_tessVertex = nullptr;
static MeshHelpers_renderMeshImmediately_t s_renderMesh = nullptr;
static MaterialPtr s_matSelection;
static uintptr_t s_renderMaterialGroup = 0;
static void (*_renderLevel_orig)(void*, void*, void*) = nullptr;
static bedrocktools::sdk::Vec3 g_playerPos{};
static bedrocktools::sdk::Vec2 g_playerRot{};
static void* g_localPlayerPtr = nullptr;

using Line = std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>;
using Lines = std::vector<Line>;

static MaterialPtr getMaterial(const char* name) {
    if (!s_renderMaterialGroup) return {};
    HashedString hs(name);
    void** vt = *reinterpret_cast<void***>(s_renderMaterialGroup);
    if (!vt || !vt[2]) return {};
    using Fn = MaterialPtr(*)(void*, const HashedString*);
    return reinterpret_cast<Fn>(vt[2])((void*)s_renderMaterialGroup, &hs);
}

static void ensureMaterials() {
    if (!s_matSelection && s_renderMaterialGroup)
        s_matSelection = getMaterial("selection_box");
}

static void addBox(Lines& out, float minx, float miny, float minz, float maxx, float maxy, float maxz) {
    bedrocktools::sdk::Vec3 p[8] = {
        {minx,miny,minz},{maxx,miny,minz},{maxx,miny,maxz},{minx,miny,maxz},
        {minx,maxy,minz},{maxx,maxy,minz},{maxx,maxy,maxz},{minx,maxy,maxz}
    };
    int e[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto& edge : e) out.push_back({p[edge[0]], p[edge[1]]});
}

/** Player-shaped wire mannequin in local space, then rotate by yaw around Y. */
static void buildBody(Lines& out, const fakeplayer::Instance& fp) {
    float yaw = fp.yaw * (float)(M_PI / 180.0);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    auto rot = [&](float lx, float ly, float lz) -> bedrocktools::sdk::Vec3 {
        // local: +Z forward, +Y up
        float wx = lx * cy - lz * sy;
        float wz = lx * sy + lz * cy;
        return {fp.feet.x + wx, fp.feet.y + ly, fp.feet.z + wz};
    };
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        bedrocktools::sdk::Vec3 c[8] = {
            rot(x0,y0,z0), rot(x1,y0,z0), rot(x1,y0,z1), rot(x0,y0,z1),
            rot(x0,y1,z0), rot(x1,y1,z0), rot(x1,y1,z1), rot(x0,y1,z1)
        };
        int e[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for (auto& edge : e) out.push_back({c[edge[0]], c[edge[1]]});
    };
    // legs
    box(-0.28f, 0.00f, -0.08f, -0.04f, 0.75f, 0.08f);
    box( 0.04f, 0.00f, -0.08f,  0.28f, 0.75f, 0.08f);
    // torso
    box(-0.30f, 0.75f, -0.12f, 0.30f, 1.40f, 0.12f);
    // arms
    box(-0.48f, 0.75f, -0.08f, -0.30f, 1.35f, 0.08f);
    box( 0.30f, 0.75f, -0.08f,  0.48f, 1.35f, 0.08f);
    // head
    box(-0.22f, 1.40f, -0.22f, 0.22f, 1.80f, 0.22f);
}

static void drawLines(void* tess, void* sc, void* mat,
                      float camX, float camY, float camZ,
                      const Lines& lines, uint32_t color) {
    if (lines.empty() || !s_tessBegin) return;
    float r = ((color >> 16) & 0xFF) / 255.f;
    float g = ((color >>  8) & 0xFF) / 255.f;
    float b = ((color      ) & 0xFF) / 255.f;
    float a = ((color >> 24) & 0xFF) / 255.f;
    s_tessBegin(tess, nullptr, 4, (int)lines.size() * 2, 0);
    s_tessColor(tess, r, g, b, a);
    for (const auto& ln : lines) {
        auto p1 = ln.first, p2 = ln.second;
        p1.x -= camX; p1.y -= camY; p1.z -= camZ;
        p2.x -= camX; p2.y -= camY; p2.z -= camZ;
        s_tessVertex(tess, p1.x, p1.y, p1.z);
        s_tessVertex(tess, p2.x, p2.y, p2.z);
    }
    char pad[64]{};
    s_renderMesh(sc, tess, mat, pad);
}

static float lookDotToFake() {
    auto& fp = fakeplayer::get();
    if (!fp.active) return -1.f;
    float cx = fp.feet.x, cy = fp.feet.y + fp.height * 0.6f, cz = fp.feet.z;
    float dx = cx - g_playerPos.x, dy = cy - g_playerPos.y, dz = cz - g_playerPos.z;
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 0.01f) return 1.f;
    dx /= len; dy /= len; dz /= len;
    float yaw = g_playerRot.y * (float)(M_PI / 180.0);
    float pitch = g_playerRot.x * (float)(M_PI / 180.0);
    float fx = -std::sin(yaw) * std::cos(pitch);
    float fy = -std::sin(pitch);
    float fz = std::cos(yaw) * std::cos(pitch);
    return fx*dx + fy*dy + fz*dz;
}

static void onTick(void* player) {
    if (!g_mod) return;
    g_localPlayerPtr = player;
    uintptr_t a = (uintptr_t)player;
    uintptr_t svc = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (svc) g_playerPos = *(bedrocktools::sdk::Vec3*)svc;
    uintptr_t rot = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mActorRotationComponent);
    if (rot) g_playerRot = *(bedrocktools::sdk::Vec2*)rot;

    auto& fp = fakeplayer::get();
    if (!g_mod->enabled) {
        fp.active = false;
        return;
    }
    // Spawn on first tick after enable (local player now valid)
    if (!fp.active && g_localPlayerPtr) {
        g_mod->spawnInFront();
    }
    // Keep active; position is fixed → zero knockback by design
    if (!fp.active) return;

    if (g_mod->faceYou) {
        float dx = g_playerPos.x - fp.feet.x;
        float dz = g_playerPos.z - fp.feet.z;
        fp.yaw = std::atan2(dx, dz) * (float)(180.0 / M_PI);
    }

    // Client-side "hit" when looking at dummy (dot > 0.92 and within 4 blocks)
    float d2 = (g_playerPos.x - fp.feet.x)*(g_playerPos.x - fp.feet.x)
             + (g_playerPos.z - fp.feet.z)*(g_playerPos.z - fp.feet.z);
    if (d2 < 16.f && lookDotToFake() > 0.92f) {
        // soft pulse so TargetESP practice feels responsive while aiming
        fp.hitFlash = std::min(1.f, fp.hitFlash + 0.08f);
    } else {
        fp.hitFlash = std::max(0.f, fp.hitFlash - 0.04f);
    }
}

static void _renderLevel_hook(void* _this, void* screenContext, void* a3) {
    if (_renderLevel_orig) _renderLevel_orig(_this, screenContext, a3);
    if (!g_mod || !g_mod->enabled || !g_mod->drawBody) return;
    auto& fp = fakeplayer::get();
    if (!fp.active) return;
    if (!s_tessBegin || !s_tessColor || !s_tessVertex || !s_renderMesh) return;
    if (!screenContext || (uintptr_t)screenContext < 0x1000) return;

    uintptr_t tessPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mTessellator);
    if (!tessPtr || tessPtr < 0x1000) return;
    void* tess = (void*)tessPtr;

    uintptr_t lrp = *(uintptr_t*)((uintptr_t)_this + bedrocktools::sdk::offsets::LevelRenderer::mLevelRendererPlayer);
    if (!lrp || lrp < 0x1000) return;
    float camX = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos);
    float camY = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 4);
    float camZ = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 8);

    ensureMaterials();
    void* mat = s_matSelection ? (void*)&s_matSelection
        : (void*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mSelectionOverlayMaterial);

    uintptr_t chPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mColorHolder);
    if (!chPtr || chPtr < 0x1000) return;
    float* ch = (float*)chPtr;
    float saved[4] = {ch[0], ch[1], ch[2], ch[3]};
    ch[0]=ch[1]=ch[2]=ch[3]=1.f;

    Lines body;
    body.reserve(64);
    buildBody(body, fp);

    // base color white; flash red when "hit"/aimed
    uint32_t col = 0xFFE0E0E0;
    if (fp.hitFlash > 0.05f) {
        float t = fp.hitFlash;
        // blend toward red
        col = 0xFF000000
            | ((uint32_t)(224 + (255-224)*t) << 16)
            | ((uint32_t)(224 * (1.f - t)) << 8)
            | ((uint32_t)(224 * (1.f - t)));
    }
    drawLines(tess, screenContext, mat, camX, camY, camZ, body, col);

    // ground marker ring
    Lines ring;
    for (int i = 0; i < 32; ++i) {
        float a0 = (float)(2.0 * M_PI * i / 32);
        float a1 = (float)(2.0 * M_PI * (i + 1) / 32);
        float r = 0.45f;
        ring.push_back({
            {fp.feet.x + std::cos(a0)*r, fp.feet.y + 0.02f, fp.feet.z + std::sin(a0)*r},
            {fp.feet.x + std::cos(a1)*r, fp.feet.y + 0.02f, fp.feet.z + std::sin(a1)*r}});
    }
    drawLines(tess, screenContext, mat, camX, camY, camZ, ring, 0xFF66CCFF);

    ch[0]=saved[0]; ch[1]=saved[1]; ch[2]=saved[2]; ch[3]=saved[3];
}

void FakePlayerModule::spawnInFront() {
    if (!g_localPlayerPtr) return;
    float yaw = g_playerRot.y * (float)(M_PI / 180.0);
    // MC forward: -sin(yaw), cos(yaw) on XZ
    float fx = -std::sin(yaw);
    float fz = std::cos(yaw);
    auto& fp = fakeplayer::get();
    fp.feet.x = g_playerPos.x + fx * distance;
    fp.feet.y = g_playerPos.y + heightOffset;
    fp.feet.z = g_playerPos.z + fz * distance;
    fp.yaw = g_playerRot.y + 180.f; // face player
    fp.height = 1.8f;
    fp.width = 0.6f;
    fp.hitFlash = 0.f;
    fakeplayer::setName(dummyName.c_str());
    fp.active = true;
}

FakePlayerModule::FakePlayerModule()
    : Module("FakePlayer", "Practice dummy 3 blocks ahead — zero knockback, TargetESP target.") {
    showInMenu = true;
    g_mod = this;
}
FakePlayerModule::~FakePlayerModule() {
    if (g_mod == this) g_mod = nullptr;
    fakeplayer::get().active = false;
}

void FakePlayerModule::onInit() {
    if (auto addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderLevel))
        m_patchTarget = (void*)addr;
    if (auto tb = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorBegin))
        s_tessBegin = (Tessellator_begin_t)tb;
    if (auto tc = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorColor))
        s_tessColor = (Tessellator_color_t)tc;
    if (auto tv = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorVertex))
        s_tessVertex = (Tessellator_vertex_t)tv;
    if (auto rm = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately2))
        s_renderMesh = (MeshHelpers_renderMeshImmediately_t)rm;
    else if (auto rm5 = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately))
        s_renderMesh = (MeshHelpers_renderMeshImmediately_t)rm5;
    if (auto rmg = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderMaterialGroupCommon)) {
        uintptr_t g = resolveADRP(reinterpret_cast<uint32_t*>(rmg), 2, 0);
        if (g) s_renderMaterialGroup = g + bedrocktools::sdk::offsets::MaterialGroup::mRenderMaterialGroupOffset;
    }
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& e) { onTick(e.player); });
}

void FakePlayerModule::applyPatch() {
    if (m_patched || !m_patchTarget) return;
    bedrocktools::hooks::install(m_patchTarget, (void*)_renderLevel_hook, (void**)&_renderLevel_orig);
    m_patched = true;
}

void FakePlayerModule::onEnable() {
    applyPatch();
    // spawn once local player is known (may be null first frame)
    if (g_localPlayerPtr) spawnInFront();
    else {
        // mark pending: onTick will spawn when player exists
        fakeplayer::get().active = false;
    }
}

void FakePlayerModule::onDisable() {
    fakeplayer::get().active = false;
}

void FakePlayerModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    distance = j.value("distance", distance);
    heightOffset = j.value("heightOffset", heightOffset);
    faceYou = j.value("faceYou", faceYou);
    drawBody = j.value("drawBody", drawBody);
    if (j.contains("dummyName") && j["dummyName"].is_string())
        dummyName = j["dummyName"].get<std::string>();
}

void FakePlayerModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["distance"] = distance;
    j["heightOffset"] = heightOffset;
    j["faceYou"] = faceYou;
    j["drawBody"] = drawBody;
    j["dummyName"] = dummyName;
}
