#include "targetesp.hpp"
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
typedef bool (*Actor_isPlayer_t)(void*);
typedef bool (*Actor_isInvisible_t)(void*);
typedef void* (*HitResult_getEntity_t)(void*);

struct DistanceSortedActor { void* mActor; float mDistance; float _pad; };
struct ActorVec { DistanceSortedActor* begin; DistanceSortedActor* end; DistanceSortedActor* cap; };
typedef ActorVec (*Actor_fetchNearbyActorsSorted_t)(void*, void*, int);

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

static TargetESPModule* g_mod = nullptr;
static Tessellator_begin_t s_tessBegin = nullptr;
static Tessellator_color_t s_tessColor = nullptr;
static Tessellator_vertex_t s_tessVertex = nullptr;
static MeshHelpers_renderMeshImmediately_t s_renderMesh = nullptr;
static Actor_isPlayer_t s_actorIsPlayer = nullptr;
static Actor_isInvisible_t s_actorIsInvisible = nullptr;
static Actor_fetchNearbyActorsSorted_t s_actorFetchNearby = nullptr;
static MaterialPtr s_matSelection;
static uintptr_t s_renderMaterialGroup = 0;
static void (*_renderLevel_orig)(void*, void*, void*) = nullptr;
static bedrocktools::sdk::Vec3 g_playerPos{};
static bedrocktools::sdk::Vec2 g_playerRot{};
static void* g_localPlayerPtr = nullptr;
static float g_phase = 0.f;

struct AABB { bedrocktools::sdk::Vec3 min, max; };
using Line = std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>;
using Lines = std::vector<Line>;

static void onTick(void* player) {
    if (!g_mod || !g_mod->enabled) return;
    g_localPlayerPtr = player;
    uintptr_t a = (uintptr_t)player;
    uintptr_t svc = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (svc) g_playerPos = *(bedrocktools::sdk::Vec3*)svc;
    uintptr_t rot = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mActorRotationComponent);
    if (rot) g_playerRot = *(bedrocktools::sdk::Vec2*)rot;
    g_phase += 0.016f * g_mod->animSpeed;
    if (g_phase > 10000.f) g_phase -= 10000.f;
}

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

static AABB getActorAABB(void* actor) {
    AABB aabb{{0,0,0},{0,0,0}};
    uintptr_t a = (uintptr_t)actor;
    uintptr_t builtIn = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (builtIn) {
        uintptr_t aabbC = *(uintptr_t*)(a + bedrocktools::sdk::offsets::Actor::mStateVectorComponent
            + bedrocktools::sdk::offsets::BuiltInActorComponents::mAABBShapeComponent);
        if (aabbC) aabb = *(AABB*)(aabbC + bedrocktools::sdk::offsets::AABBShapeComponent::mAABB);
    }
    return aabb;
}

static float dist2(const bedrocktools::sdk::Vec3& a, const bedrocktools::sdk::Vec3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

/** Approximate view-dot toward entity center (higher = more centered on crosshair). */
static float lookScore(const AABB& aabb) {
    float cx = (aabb.min.x + aabb.max.x) * 0.5f;
    float cy = (aabb.min.y + aabb.max.y) * 0.5f;
    float cz = (aabb.min.z + aabb.max.z) * 0.5f;
    float dx = cx - g_playerPos.x;
    float dy = cy - g_playerPos.y;
    float dz = cz - g_playerPos.z;
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) return 1.f;
    dx /= len; dy /= len; dz /= len;
    // MC yaw/pitch: yaw degrees, pitch degrees — convert roughly
    float yaw = g_playerRot.y * (float)(M_PI / 180.0);
    float pitch = g_playerRot.x * (float)(M_PI / 180.0);
    float fx = -std::sin(yaw) * std::cos(pitch);
    float fy = -std::sin(pitch);
    float fz = std::cos(yaw) * std::cos(pitch);
    return fx*dx + fy*dy + fz*dz;
}

static void addRing(Lines& out, float cx, float cz, float y, float rx, float rz, int segs, float spin) {
    for (int i = 0; i < segs; ++i) {
        float a0 = (float)(2.0 * M_PI * i / segs) + spin;
        float a1 = (float)(2.0 * M_PI * (i + 1) / segs) + spin;
        out.push_back({{cx + std::cos(a0)*rx, y, cz + std::sin(a0)*rz},
                       {cx + std::cos(a1)*rx, y, cz + std::sin(a1)*rz}});
    }
}

/** Double ring for thickness. */
static void addThickRing(Lines& out, float cx, float cz, float y, float rx, float rz, int segs, float spin, float thick) {
    addRing(out, cx, cz, y, rx, rz, segs, spin);
    if (thick > 0.5f) {
        addRing(out, cx, cz, y + 0.015f * thick, rx * 1.02f, rz * 1.02f, segs, spin);
        addRing(out, cx, cz, y - 0.015f * thick, rx * 0.98f, rz * 0.98f, segs, spin);
    }
}

static void addDiamond(Lines& out, float x, float y, float z, float s) {
    bedrocktools::sdk::Vec3 n{x, y+s, z}, e{x+s, y, z}, so{x, y-s, z}, w{x-s, y, z};
    out.push_back({n,e}); out.push_back({e,so}); out.push_back({so,w}); out.push_back({w,n});
}

static void addBox(Lines& out, float x, float y, float z, float sx, float sy, float sz) {
    bedrocktools::sdk::Vec3 p[8] = {
        {x-sx,y-sy,z-sz},{x+sx,y-sy,z-sz},{x+sx,y-sy,z+sz},{x-sx,y-sy,z+sz},
        {x-sx,y+sy,z-sz},{x+sx,y+sy,z-sz},{x+sx,y+sy,z+sz},{x-sx,y+sy,z+sz}
    };
    int e[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto& edge : e) out.push_back({p[edge[0]], p[edge[1]]});
}

static void styleChains(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    float mid = y0 + (y2 - y0) * 0.4f;
    addThickRing(A, cx, cz, y0, rx, rz, 56, g_phase * 0.35f, thick);
    addThickRing(A, cx, cz, mid, rx * 0.88f, rz * 0.88f, 40, -g_phase * 0.28f, thick);
    addThickRing(A, cx, cz, y2, rx * 0.5f, rz * 0.5f, 28, g_phase * 0.5f, thick * 0.7f);
    int links = std::clamp(g_mod->chainLinks, 6, 28);
    for (int i = 0; i < links; ++i) {
        float base = g_phase * 1.4f + (float)(2.0 * M_PI * i / links);
        float elev = y0 + (y2 - y0) * (0.15f + 0.7f * (0.5f + 0.5f * std::sin(base * 1.6f + i * 0.7f)));
        float ox = cx + std::cos(base) * rx * 1.18f;
        float oz = cz + std::sin(base) * rz * 1.18f;
        float s = 0.11f + 0.05f * std::sin(base * 2.8f);
        addDiamond(B, ox, elev, oz, s);
        // chain connector to neighbor
        float base2 = base + (float)(2.0 * M_PI / links);
        float elev2 = y0 + (y2 - y0) * (0.15f + 0.7f * (0.5f + 0.5f * std::sin(base2 * 1.6f + (i+1) * 0.7f)));
        float ox2 = cx + std::cos(base2) * rx * 1.18f;
        float oz2 = cz + std::sin(base2) * rz * 1.18f;
        B.push_back({{ox, elev, oz}, {ox2, elev2, oz2}});
        // outer spike
        B.push_back({{ox, elev, oz}, {cx + std::cos(base)*rx*1.5f, elev, cz + std::sin(base)*rz*1.5f}});
    }
}

static void styleNova(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    float mid = (y0 + y2) * 0.5f;
    addThickRing(A, cx, cz, mid, rx * 0.28f, rz * 0.28f, 32, g_phase, thick);
    float expand = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(g_phase * 2.4f));
    for (int i = 0; i < 20; ++i) {
        float a = (float)(2.0 * M_PI * i / 20) + g_phase * 0.25f;
        float c = std::cos(a), s = std::sin(a);
        float len = expand * (1.0f + 0.15f * std::sin(g_phase * 3.f + i));
        B.push_back({{cx + c*rx*0.2f, mid, cz + s*rz*0.2f},
                     {cx + c*rx*len*1.55f, mid + 0.2f*std::sin(a*2+g_phase), cz + s*rz*len*1.55f}});
        if (i % 2 == 0)
            A.push_back({{cx, mid, cz}, {cx + c*rx*0.85f, y0 + (y2-y0)*0.75f, cz + s*rz*0.85f}});
    }
}

static void styleWild(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float) {
    addRing(A, cx, cz, y0, rx, rz, 24, g_phase * 0.5f);
    for (int i = 0; i < 28; ++i) {
        float a = g_phase * 2.3f + i * 1.6180339887f;
        float t = 0.2f + 0.8f * (0.5f + 0.5f * std::sin(a * 0.85f + i));
        float elev = y0 + (y2 - y0) * t;
        float r = rx * (0.65f + 0.7f * std::sin(a * 1.4f));
        float ox = cx + std::cos(a) * r;
        float oz = cz + std::sin(a * 1.13f) * r;
        float ox2 = cx + std::cos(a + 0.55f) * r * 1.25f;
        float oz2 = cz + std::sin(a + 0.6f) * r * 0.85f;
        (i & 1 ? B : A).push_back({{ox, elev, oz}, {ox2, elev + 0.3f * std::sin(a * 2.f), oz2}});
    }
}

static void styleRhombus(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    float mid = (y0 + y2) * 0.5f;
    float spin = g_phase * 0.75f;
    float s = std::max(rx, rz) * 1.15f;
    for (int layer = 0; layer < 4; ++layer) {
        float sc = 1.0f - layer * 0.18f;
        float y = mid + (layer - 1.5f) * 0.28f;
        float ang = spin + layer * 0.35f;
        bedrocktools::sdk::Vec3 pts[4];
        for (int i = 0; i < 4; ++i) {
            float a = ang + (float)(M_PI * 0.5 * i);
            pts[i] = {cx + std::cos(a)*s*sc, y, cz + std::sin(a)*s*sc};
        }
        Lines& dest = (layer < 2) ? A : B;
        for (int i = 0; i < 4; ++i) dest.push_back({pts[i], pts[(i+1)&3]});
        if (thick > 0.5f && layer == 0) {
            for (int i = 0; i < 4; ++i) {
                float a = ang + (float)(M_PI * 0.5 * i);
                float sc2 = sc * 1.04f;
                bedrocktools::sdk::Vec3 p2{cx + std::cos(a)*s*sc2, y + 0.02f, cz + std::sin(a)*s*sc2};
                A.push_back({pts[i], p2});
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        float a = spin + (float)(M_PI * 0.5 * i);
        float x = cx + std::cos(a)*s*0.9f, z = cz + std::sin(a)*s*0.9f;
        A.push_back({{x, y0, z}, {x, y2, z}});
    }
}

static void styleScanner(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    addThickRing(A, cx, cz, y0, rx, rz, 48, 0.f, thick);
    addThickRing(A, cx, cz, y2, rx * 0.72f, rz * 0.72f, 36, 0.f, thick);
    float t = 0.5f + 0.5f * std::sin(g_phase * 2.0f);
    float y = y0 + (y2 - y0) * t;
    addThickRing(B, cx, cz, y, rx * 1.2f, rz * 1.2f, 48, g_phase * 0.5f, thick);
    // grid ticks on scan plane
    for (int i = 0; i < 16; ++i) {
        float a = (float)(2.0 * M_PI * i / 16);
        float x = cx + std::cos(a)*rx*1.05f, z = cz + std::sin(a)*rz*1.05f;
        A.push_back({{x, y - 0.12f, z}, {x, y + 0.12f, z}});
    }
    // vertical rails
    for (int i = 0; i < 8; ++i) {
        float a = (float)(2.0 * M_PI * i / 8);
        float x = cx + std::cos(a)*rx, z = cz + std::sin(a)*rz;
        B.push_back({{x, y0, z}, {x, y2, z}});
    }
}

static void styleVega(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    const int n = 18;
    for (int i = 0; i < n; ++i) {
        float a = (float)(2.0 * M_PI * i / n) + g_phase * 0.18f;
        float x = cx + std::cos(a)*rx, z = cz + std::sin(a)*rz;
        float wave = 0.12f * std::sin(g_phase * 3.2f + i * 0.8f);
        (i & 1 ? B : A).push_back({{x, y0 + wave, z}, {x, y2 + wave, z}});
    }
    addThickRing(A, cx, cz, y0, rx * 0.98f, rz * 0.98f, 36, g_phase * 0.2f, thick);
    addThickRing(B, cx, cz, y2, rx * 0.7f, rz * 0.7f, 28, -g_phase * 0.2f, thick);
}

static void styleSwords(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float) {
    float mid = (y0 + y2) * 0.5f;
    addRing(A, cx, cz, y0, rx * 0.45f, rz * 0.45f, 24, g_phase);
    const int blades = 8;
    for (int i = 0; i < blades; ++i) {
        float a = g_phase * 0.95f + (float)(2.0 * M_PI * i / blades);
        float c = std::cos(a), s = std::sin(a);
        bedrocktools::sdk::Vec3 tip{cx + c*rx*1.4f, mid + 0.55f, cz + s*rz*1.4f};
        bedrocktools::sdk::Vec3 hilt{cx + c*rx*0.3f, mid - 0.25f, cz + s*rz*0.3f};
        bedrocktools::sdk::Vec3 gL{hilt.x - s*0.18f, hilt.y, hilt.z + c*0.18f};
        bedrocktools::sdk::Vec3 gR{hilt.x + s*0.18f, hilt.y, hilt.z - c*0.18f};
        B.push_back({hilt, tip});
        A.push_back({gL, gR});
        // blade edge double
        B.push_back({{hilt.x - s*0.03f, hilt.y, hilt.z + c*0.03f},
                     {tip.x - s*0.03f, tip.y, tip.z + c*0.03f}});
        // tip cross
        B.push_back({{tip.x - s*0.22f, tip.y - 0.12f, tip.z + c*0.22f},
                     {tip.x + s*0.22f, tip.y + 0.12f, tip.z - c*0.22f}});
    }
}

static void styleCrystals(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    addThickRing(A, cx, cz, y0, rx, rz, 36, g_phase * 0.22f, thick);
    int n = std::clamp(g_mod->chainLinks, 6, 18);
    for (int i = 0; i < n; ++i) {
        float a = g_phase * 0.85f + (float)(2.0 * M_PI * i / n);
        float elev = y0 + (y2 - y0) * (0.2f + 0.55f * (i / (float)n));
        float ox = cx + std::cos(a) * rx * 1.12f;
        float oz = cz + std::sin(a) * rz * 1.12f;
        float h = 0.28f + 0.1f * std::sin(g_phase * 2.2f + i);
        bedrocktools::sdk::Vec3 top{ox, elev + h, oz};
        bedrocktools::sdk::Vec3 b0{ox + 0.14f, elev - h * 0.25f, oz};
        bedrocktools::sdk::Vec3 b1{ox - 0.07f, elev - h * 0.25f, oz + 0.12f};
        bedrocktools::sdk::Vec3 b2{ox - 0.07f, elev - h * 0.25f, oz - 0.12f};
        B.push_back({top, b0}); B.push_back({top, b1}); B.push_back({top, b2});
        B.push_back({b0, b1}); B.push_back({b1, b2}); B.push_back({b2, b0});
        // inner sparkle
        A.push_back({{ox, elev, oz}, {cx, elev, cz}});
    }
}

static void styleJello(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    for (int layer = 0; layer < 8; ++layer) {
        float t = layer / 7.f;
        float y = y0 + (y2 - y0) * t;
        float wobble = 1.0f + 0.2f * std::sin(g_phase * 2.6f + layer * 0.85f);
        float spin = g_phase * 0.35f * (layer % 2 ? 1.f : -1.f);
        float sc = wobble * (1.f - t * 0.12f);
        addThickRing(layer & 1 ? B : A, cx, cz, y, rx * sc, rz * sc, 32, spin, thick * 0.6f);
    }
}

static void styleGhost(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float) {
    for (int layer = 0; layer < 4; ++layer) {
        float y = y0 + (y2 - y0) * (0.1f + 0.28f * layer);
        float sc = 1.0f + layer * 0.14f + 0.05f * std::sin(g_phase + layer);
        int segs = 40;
        for (int i = 0; i < segs; i += 2) {
            float a0 = (float)(2.0 * M_PI * i / segs) + g_phase * 0.18f * (layer + 1);
            float a1 = (float)(2.0 * M_PI * (i + 1) / segs) + g_phase * 0.18f * (layer + 1);
            (layer & 1 ? B : A).push_back({
                {cx + std::cos(a0)*rx*sc, y, cz + std::sin(a0)*rz*sc},
                {cx + std::cos(a1)*rx*sc, y, cz + std::sin(a1)*rz*sc}});
        }
    }
}

static void styleBubble(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float) {
    float mid = (y0 + y2) * 0.5f;
    float R = std::max(rx, (y2 - y0) * 0.55f) * 1.2f;
    for (int i = 0; i < 6; ++i) {
        float t = (i + 1) / 7.f;
        float y = mid + (t - 0.5f) * 2.f * R * 0.9f;
        float rr = R * std::sin((float)M_PI * t);
        addRing(i & 1 ? B : A, cx, cz, y, rr, rr, 28, g_phase * 0.12f);
    }
    for (int i = 0; i < 10; ++i) {
        float a = (float)(2.0 * M_PI * i / 10) + g_phase * 0.08f;
        for (int j = 0; j < 12; ++j) {
            float t0 = (float)M_PI * j / 12, t1 = (float)M_PI * (j + 1) / 12;
            float y0s = mid + std::cos(t0) * R * 0.9f;
            float y1s = mid + std::cos(t1) * R * 0.9f;
            float r0 = std::sin(t0) * R, r1 = std::sin(t1) * R;
            A.push_back({{cx + std::cos(a)*r0, y0s, cz + std::sin(a)*r0},
                         {cx + std::cos(a)*r1, y1s, cz + std::sin(a)*r1}});
        }
    }
}

static void styleHelix(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    addThickRing(A, cx, cz, y0, rx * 0.9f, rz * 0.9f, 32, g_phase, thick);
    const int steps = 48;
    for (int strand = 0; strand < 2; ++strand) {
        float off = strand * (float)M_PI;
        for (int i = 0; i < steps; ++i) {
            float t0 = i / (float)steps, t1 = (i + 1) / (float)steps;
            float a0 = g_phase * 1.5f + t0 * 4.f * (float)M_PI + off;
            float a1 = g_phase * 1.5f + t1 * 4.f * (float)M_PI + off;
            float yA = y0 + (y2 - y0) * t0;
            float yB = y0 + (y2 - y0) * t1;
            (strand ? B : A).push_back({
                {cx + std::cos(a0)*rx, yA, cz + std::sin(a0)*rz},
                {cx + std::cos(a1)*rx, yB, cz + std::sin(a1)*rz}});
        }
    }
}

static void styleOrbit(Lines& A, Lines& B, float cx, float cz, float y0, float y2, float rx, float rz, float thick) {
    float mid = (y0 + y2) * 0.5f;
    addThickRing(A, cx, cz, mid, rx * 0.4f, rz * 0.4f, 24, g_phase, thick);
    // 3 orbital planes
    for (int plane = 0; plane < 3; ++plane) {
        float tilt = plane * (float)(M_PI / 3);
        float spin = g_phase * (1.0f + plane * 0.3f);
        for (int i = 0; i < 36; ++i) {
            float a0 = spin + (float)(2.0 * M_PI * i / 36);
            float a1 = spin + (float)(2.0 * M_PI * (i + 1) / 36);
            float x0 = std::cos(a0) * rx * 1.25f, z0 = std::sin(a0) * rx * 1.25f;
            float x1 = std::cos(a1) * rx * 1.25f, z1 = std::sin(a1) * rx * 1.25f;
            float yA = mid + z0 * std::sin(tilt) * 0.6f;
            float yB = mid + z1 * std::sin(tilt) * 0.6f;
            float zz0 = z0 * std::cos(tilt), zz1 = z1 * std::cos(tilt);
            (plane == 1 ? B : A).push_back({{cx + x0, yA, cz + zz0}, {cx + x1, yB, cz + zz1}});
        }
    }
}

static void renderTarget(void* ent, void* tess, void* sc, void* mat,
                         float camX, float camY, float camZ) {
    AABB aabb = getActorAABB(ent);
    float cx = (aabb.min.x + aabb.max.x) * 0.5f;
    float cz = (aabb.min.z + aabb.max.z) * 0.5f;
    float y0 = aabb.min.y + g_mod->ringHeight;
    float y2 = aabb.max.y - 0.04f;
    if (y2 <= y0) y2 = y0 + 1.6f;
    float pulse = 1.0f + 0.07f * std::sin(g_phase * 2.4f);
    float rx = ((aabb.max.x - aabb.min.x) * 0.55f + 0.18f) * pulse;
    float rz = ((aabb.max.z - aabb.min.z) * 0.55f + 0.18f) * pulse;
    float thick = std::clamp(g_mod->thickness, 0.3f, 2.5f);

    Lines primary, accent;
    primary.reserve(400);
    accent.reserve(400);

    switch (std::clamp(g_mod->style, 0, (int)TargetESPModule::StyleCount - 1)) {
        case TargetESPModule::StyleNova:     styleNova(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleWild:     styleWild(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleRhombus:  styleRhombus(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleScanner:  styleScanner(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleVega:     styleVega(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleSwords:   styleSwords(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleCrystals: styleCrystals(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleJello:    styleJello(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleGhost:    styleGhost(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleBubble:   styleBubble(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleHelix:    styleHelix(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleOrbit:    styleOrbit(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleChains:
        default:                             styleChains(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
    }

    auto drawLines = [&](const Lines& lines, uint32_t color) {
        if (lines.empty()) return;
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
    };

    drawLines(primary, g_mod->ringColor);
    drawLines(accent, g_mod->chainColor);
}


static void renderFakePlayerTarget(void* tess, void* sc, void* mat,
                                   float camX, float camY, float camZ) {
    auto& fp = fakeplayer::get();
    if (!fp.active) return;
    // Build a synthetic AABB matching the dummy and run style code via local vars
    float cx = fp.feet.x;
    float cz = fp.feet.z;
    float y0 = fp.feet.y + g_mod->ringHeight;
    float y2 = fp.feet.y + fp.height - 0.04f;
    float pulse = 1.0f + 0.07f * std::sin(g_phase * 2.4f);
    if (fp.hitFlash > 0.f) pulse += 0.12f * fp.hitFlash;
    float rx = (fp.width * 0.55f + 0.18f) * pulse;
    float rz = rx;
    float thick = std::clamp(g_mod->thickness, 0.3f, 2.5f);

    Lines primary, accent;
    primary.reserve(400);
    accent.reserve(400);

    switch (std::clamp(g_mod->style, 0, (int)TargetESPModule::StyleCount - 1)) {
        case TargetESPModule::StyleNova:     styleNova(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleWild:     styleWild(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleRhombus:  styleRhombus(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleScanner:  styleScanner(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleVega:     styleVega(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleSwords:   styleSwords(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleCrystals: styleCrystals(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleJello:    styleJello(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleGhost:    styleGhost(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleBubble:   styleBubble(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleHelix:    styleHelix(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleOrbit:    styleOrbit(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
        case TargetESPModule::StyleChains:
        default:                             styleChains(primary, accent, cx, cz, y0, y2, rx, rz, thick); break;
    }

    auto drawLines = [&](const Lines& lines, uint32_t color) {
        if (lines.empty()) return;
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
    };

    uint32_t ring = g_mod->ringColor;
    uint32_t chain = g_mod->chainColor;
    if (fp.hitFlash > 0.1f) {
        // flash warmer on aim/hit
        chain = 0xFFFF3333;
    }
    drawLines(primary, ring);
    drawLines(accent, chain);
}

static void _renderLevel_hook(void* _this, void* screenContext, void* a3) {
    if (_renderLevel_orig) _renderLevel_orig(_this, screenContext, a3);
    if (!g_mod || !g_mod->enabled || !g_localPlayerPtr) return;
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
    ch[0] = ch[1] = ch[2] = ch[3] = 1.f;

    if (s_actorFetchNearby) {
        bedrocktools::sdk::Vec3 extent{g_mod->maxDistance, g_mod->maxDistance, g_mod->maxDistance};
        ActorVec actors = s_actorFetchNearby(g_localPlayerPtr, &extent, 1);

        void* bestLook = nullptr;
        float bestScore = 0.55f; // min dot to count as "looking at"

        if (actors.begin && actors.end) {
            for (auto* it = actors.begin; it < actors.end; ++it) {
                void* ent = it->mActor;
                if (!ent || ent == g_localPlayerPtr) continue;
                if (s_actorIsPlayer && !s_actorIsPlayer(ent)) continue;
                if (s_actorIsInvisible && s_actorIsInvisible(ent)) continue;
                if (it->mDistance > g_mod->maxDistance) continue;

                if (g_mod->onlyLookingAt) {
                    AABB aabb = getActorAABB(ent);
                    float sc = lookScore(aabb);
                    if (sc > bestScore) { bestScore = sc; bestLook = ent; }
                } else {
                    renderTarget(ent, tess, screenContext, mat, camX, camY, camZ);
                }
            }
        }
        if (g_mod->onlyLookingAt && bestLook)
            renderTarget(bestLook, tess, screenContext, mat, camX, camY, camZ);
    }
    if (g_mod->showSelf && g_localPlayerPtr)
        renderTarget(g_localPlayerPtr, tess, screenContext, mat, camX, camY, camZ);

    // Practice dummy for TargetESP testing
    renderFakePlayerTarget(tess, screenContext, mat, camX, camY, camZ);

    ch[0]=saved[0]; ch[1]=saved[1]; ch[2]=saved[2]; ch[3]=saved[3];
}

TargetESPModule::TargetESPModule()
    : Module("TargetESP", "Native TargetESP — 13 styles (Chains/Nova/Nursultan/Swords/Helix/...).") {
    showInMenu = true;
    g_mod = this;
}
TargetESPModule::~TargetESPModule() { if (g_mod == this) g_mod = nullptr; }

void TargetESPModule::onInit() {
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
    if (auto aip = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsPlayer))
        s_actorIsPlayer = (Actor_isPlayer_t)aip;
    if (auto aii = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsInvisible))
        s_actorIsInvisible = (Actor_isInvisible_t)aii;
    if (auto afn = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorFetchNearbyActorsSorted))
        s_actorFetchNearby = (Actor_fetchNearbyActorsSorted_t)afn;
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& e) { onTick(e.player); });
}

void TargetESPModule::applyPatch() {
    if (m_patched || !m_patchTarget) return;
    bedrocktools::hooks::install(m_patchTarget, (void*)_renderLevel_hook, (void**)&_renderLevel_orig);
    m_patched = true;
}
void TargetESPModule::onEnable() { applyPatch(); }
void TargetESPModule::onDisable() {}

void TargetESPModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    showSelf = j.value("showSelf", showSelf);
    onlyLookingAt = j.value("onlyLookingAt", onlyLookingAt);
    maxDistance = j.value("maxDistance", maxDistance);
    ringHeight = j.value("ringHeight", ringHeight);
    animSpeed = j.value("animSpeed", animSpeed);
    thickness = j.value("thickness", thickness);
    chainLinks = j.value("chainLinks", chainLinks);
    style = std::clamp(j.value("style", style), 0, (int)StyleCount - 1);
    auto parseHex = [](const nlohmann::json& j, const char* k, uint32_t& out) {
        if (j.contains(k) && j[k].is_string()) {
            unsigned v = 0;
            if (sscanf(j[k].get<std::string>().c_str(), "#%x", &v) == 1) out = v;
        }
    };
    parseHex(j, "ringColor", ringColor);
    parseHex(j, "chainColor", chainColor);
    parseHex(j, "accentColor", accentColor);
}

void TargetESPModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["showSelf"] = showSelf;
    j["onlyLookingAt"] = onlyLookingAt;
    j["maxDistance"] = maxDistance;
    j["ringHeight"] = ringHeight;
    j["animSpeed"] = animSpeed;
    j["thickness"] = thickness;
    j["chainLinks"] = chainLinks;
    // 0 Chains 1 Nova 2 Wild 3 Rhombus 4 Scanner 5 Vega 6 Swords 7 Crystals 8 Jello 9 Ghost 10 Bubble 11 Helix 12 Orbit
    j["style"] = style;
    char hex[16];
    snprintf(hex, sizeof(hex), "#%08X", ringColor); j["ringColor"] = std::string(hex);
    snprintf(hex, sizeof(hex), "#%08X", chainColor); j["chainColor"] = std::string(hex);
    snprintf(hex, sizeof(hex), "#%08X", accentColor); j["accentColor"] = std::string(hex);
}
