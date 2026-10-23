#include "targetesp.hpp"
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef void (*Tessellator_begin_t)(void* tessellator, void* debugCallback, int primitiveMode, int vertexCount, int noIndices);
typedef void (*Tessellator_color_t)(void* tessellator, float r, float g, float b, float a);
typedef void (*Tessellator_vertex_t)(void* tessellator, float x, float y, float z);
typedef void (*MeshHelpers_renderMeshImmediately_t)(void* screenContext, void* tessellator, void* material, char* pad);

typedef bool (*Actor_isPlayer_t)(void* actor);
typedef bool (*Actor_isInvisible_t)(void* actor);

struct DistanceSortedActor {
    void* mActor;
    float mDistance;
    float _pad;
};

struct ActorVec {
    DistanceSortedActor* begin;
    DistanceSortedActor* end;
    DistanceSortedActor* cap;
};

typedef ActorVec (*Actor_fetchNearbyActorsSorted_t)(void* actor, void* extent, int actorType);

struct HashedString {
    uint64_t mStrHash;
    std::string mStr;
    mutable const HashedString* mLastMatch;

    HashedString() : mStrHash(0), mStr(), mLastMatch(nullptr) {}
    explicit HashedString(const char* str) : mLastMatch(nullptr) {
        mStr = str ? str : "";
        mStrHash = computeHash(mStr);
    }
private:
    static uint64_t computeHash(const std::string& str) {
        if (str.empty()) return 0;
        constexpr uint64_t kOffset = 0xCBF29CE484222325ULL;
        constexpr uint64_t kPrime = 0x100000001B3ULL;
        uint64_t hash = kOffset;
        for (char ch : str)
            hash = static_cast<uint64_t>(static_cast<unsigned char>(ch)) ^ (kPrime * hash);
        return hash;
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
            sharedPtrData[0] = o.sharedPtrData[0];
            sharedPtrData[1] = o.sharedPtrData[1];
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
        if ((insn & 0x9F000000) == 0x90000000) {
            uint32_t rd = insn & 0x1F;
            if (rd != targetReg) continue;
            int64_t immhi = (insn >> 5) & 0x7FFFF;
            int64_t immlo = (insn >> 29) & 0x3;
            int64_t imm = (immhi << 2) | immlo;
            imm = (imm << 43) >> 43;
            uintptr_t page = ((uintptr_t)&insns[i] & ~0xFFFULL) + (uintptr_t)(imm << 12);
            for (size_t j = i + 1; j < count && j < i + 8; ++j) {
                uint32_t add = insns[j];
                if ((add & 0xFF000000) == 0x91000000 &&
                    ((add >> 5) & 0x1F) == targetReg &&
                    (add & 0x1F) == targetReg) {
                    uint32_t imm12 = (add >> 10) & 0xFFF;
                    if (add & 0x400000) imm12 <<= 12;
                    return page + imm12;
                }
                if ((add & 0x1F) == targetReg) break;
            }
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
static void (*_renderLevel_orig)(void* _this, void* screenContext, void* a3) = nullptr;
static bedrocktools::sdk::Vec3 g_playerPos = {0.f, 0.f, 0.f};
static void* g_localPlayerPtr = nullptr;
static float g_animPhase = 0.f;

struct AABB {
    bedrocktools::sdk::Vec3 min;
    bedrocktools::sdk::Vec3 max;
};

static void onTick(void* player) {
    if (!g_mod || !g_mod->enabled) return;
    g_localPlayerPtr = player;
    uintptr_t svc = *(uintptr_t*)((uintptr_t)player + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (svc) g_playerPos = *(bedrocktools::sdk::Vec3*)svc;
    g_animPhase += 0.016f * (g_mod ? g_mod->animSpeed : 1.f);
    if (g_animPhase > 1000.f) g_animPhase -= 1000.f;
}

static MaterialPtr getMaterial(const char* name) {
    if (!s_renderMaterialGroup) return {};
    HashedString hs(name);
    void** vtable = *reinterpret_cast<void***>(s_renderMaterialGroup);
    if (!vtable || !vtable[2]) return {};
    using getMat_t = MaterialPtr(*)(void*, const HashedString*);
    return reinterpret_cast<getMat_t>(vtable[2])((void*)s_renderMaterialGroup, &hs);
}

static void ensureMaterials() {
    if (s_matSelection) return;
    if (!s_renderMaterialGroup) return;
    s_matSelection = getMaterial("selection_box");
}

static AABB getActorAABB(void* actor) {
    AABB aabb = {{0,0,0},{0,0,0}};
    uintptr_t actorAddr = (uintptr_t)actor;
    uintptr_t builtInPtr = *(uintptr_t*)(actorAddr + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (builtInPtr) {
        uintptr_t aabbComponentPtr = *(uintptr_t*)(actorAddr + bedrocktools::sdk::offsets::Actor::mStateVectorComponent + bedrocktools::sdk::offsets::BuiltInActorComponents::mAABBShapeComponent);
        if (aabbComponentPtr)
            aabb = *(AABB*)(aabbComponentPtr + bedrocktools::sdk::offsets::AABBShapeComponent::mAABB);
    }
    return aabb;
}

static void _renderLevel_hook(void* _this, void* screenContext, void* a3) {
    if (_renderLevel_orig) _renderLevel_orig(_this, screenContext, a3);

    if (!g_mod || !g_mod->enabled) return;
    if (!g_localPlayerPtr) return;
    if (!s_tessBegin || !s_tessColor || !s_tessVertex || !s_renderMesh) return;
    if (!screenContext || (uintptr_t)screenContext < 0x1000) return;

    uintptr_t tessellatorPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mTessellator);
    if (!tessellatorPtr || tessellatorPtr < 0x1000) return;
    void* tessellator = (void*)tessellatorPtr;

    uintptr_t lrpPtr = *(uintptr_t*)((uintptr_t)_this + bedrocktools::sdk::offsets::LevelRenderer::mLevelRendererPlayer);
    if (!lrpPtr || lrpPtr < 0x1000) return;

    float camX = *(float*)(lrpPtr + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos);
    float camY = *(float*)(lrpPtr + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 4);
    float camZ = *(float*)(lrpPtr + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 8);

    ensureMaterials();
    void* matInner = s_matSelection ? (void*)&s_matSelection
                                    : (void*)(lrpPtr + bedrocktools::sdk::offsets::LevelRendererPlayer::mSelectionOverlayMaterial);

    uintptr_t colorHolderPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mColorHolder);
    if (!colorHolderPtr || colorHolderPtr < 0x1000) return;
    float* colorHolder = (float*)colorHolderPtr;
    float savedColor[4] = { colorHolder[0], colorHolder[1], colorHolder[2], colorHolder[3] };
    colorHolder[0] = colorHolder[1] = colorHolder[2] = colorHolder[3] = 1.0f;

    auto drawLines = [&](const std::vector<std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>>& lines, uint32_t color) {
        if (lines.empty()) return;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >>  8) & 0xFF) / 255.0f;
        float b = ((color      ) & 0xFF) / 255.0f;
        float a = ((color >> 24) & 0xFF) / 255.0f;

        s_tessBegin(tessellator, nullptr, 4, static_cast<int>(lines.size() * 2), 0);
        s_tessColor(tessellator, r, g, b, a);
        for (const auto& line : lines) {
            bedrocktools::sdk::Vec3 p1 = line.first;
            bedrocktools::sdk::Vec3 p2 = line.second;
            p1.x -= camX; p1.y -= camY; p1.z -= camZ;
            p2.x -= camX; p2.y -= camY; p2.z -= camZ;
            s_tessVertex(tessellator, p1.x, p1.y, p1.z);
            s_tessVertex(tessellator, p2.x, p2.y, p2.z);
        }
        char pad[64]{};
        s_renderMesh(screenContext, tessellator, matInner, pad);
    };

    auto renderTarget = [&](void* ent) {
        AABB aabb = getActorAABB(ent);
        float cx = (aabb.min.x + aabb.max.x) * 0.5f;
        float cz = (aabb.min.z + aabb.max.z) * 0.5f;
        float y0 = aabb.min.y + g_mod->ringHeight;
        float y1 = aabb.min.y + (aabb.max.y - aabb.min.y) * 0.45f;
        float y2 = aabb.max.y - 0.05f;
        float rx = (aabb.max.x - aabb.min.x) * 0.55f + 0.15f;
        float rz = (aabb.max.z - aabb.min.z) * 0.55f + 0.15f;
        float pulse = 1.0f + 0.08f * std::sin(g_animPhase * 2.2f);
        rx *= pulse;
        rz *= pulse;

        const int segs = 48;
        std::vector<std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>> ringLines;
        ringLines.reserve(segs * 3 + g_mod->chainLinks * 12);

        auto addRing = [&](float y, float scale, int segments) {
            for (int i = 0; i < segments; ++i) {
                float a0 = (float)(2.0 * M_PI * i / segments) + g_animPhase * 0.4f;
                float a1 = (float)(2.0 * M_PI * (i + 1) / segments) + g_animPhase * 0.4f;
                bedrocktools::sdk::Vec3 p0{
                    cx + std::cos(a0) * rx * scale,
                    y,
                    cz + std::sin(a0) * rz * scale
                };
                bedrocktools::sdk::Vec3 p1{
                    cx + std::cos(a1) * rx * scale,
                    y,
                    cz + std::sin(a1) * rz * scale
                };
                ringLines.push_back({p0, p1});
            }
        };

        // Main white target rings (like the circle in the video)
        addRing(y0, 1.0f, segs);
        addRing(y1, 0.85f, segs / 2);
        addRing(y2, 0.55f, 24);

        // Orbiting "chain" segments — short diamonds spinning around the body
        const int links = std::max(4, std::min(24, g_mod->chainLinks));
        for (int i = 0; i < links; ++i) {
            float base = g_animPhase * 1.3f + (float)(2.0 * M_PI * i / links);
            float elev = y0 + (y2 - y0) * (0.2f + 0.6f * (0.5f + 0.5f * std::sin(base * 1.7f + i)));
            float ox = cx + std::cos(base) * rx * 1.15f;
            float oz = cz + std::sin(base) * rz * 1.15f;
            float s = 0.12f + 0.04f * std::sin(base * 3.f);
            // diamond (chain-link proxy)
            bedrocktools::sdk::Vec3 n{ox, elev + s, oz};
            bedrocktools::sdk::Vec3 e{ox + s, elev, oz};
            bedrocktools::sdk::Vec3 so{ox, elev - s, oz};
            bedrocktools::sdk::Vec3 w{ox - s, elev, oz};
            ringLines.push_back({n, e});
            ringLines.push_back({e, so});
            ringLines.push_back({so, w});
            ringLines.push_back({w, n});
            // outer radial streak
            bedrocktools::sdk::Vec3 outer{
                cx + std::cos(base) * rx * 1.45f,
                elev,
                cz + std::sin(base) * rz * 1.45f
            };
            ringLines.push_back({{ox, elev, oz}, outer});
        }

        drawLines(ringLines, g_mod->ringColor);

        // Second pass: chain color accents only on orbiting links
        std::vector<std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>> chainOnly;
        chainOnly.reserve(links * 5);
        for (int i = 0; i < links; ++i) {
            float base = g_animPhase * 1.3f + (float)(2.0 * M_PI * i / links) + 0.15f;
            float elev = y0 + (y2 - y0) * (0.25f + 0.5f * (0.5f + 0.5f * std::cos(base * 1.4f)));
            float ox = cx + std::cos(base) * rx * 1.25f;
            float oz = cz + std::sin(base) * rz * 1.25f;
            float s = 0.16f;
            bedrocktools::sdk::Vec3 n{ox, elev + s, oz};
            bedrocktools::sdk::Vec3 e{ox + s * 0.7f, elev, oz + s * 0.3f};
            bedrocktools::sdk::Vec3 so{ox, elev - s, oz};
            bedrocktools::sdk::Vec3 w{ox - s * 0.7f, elev, oz - s * 0.3f};
            chainOnly.push_back({n, e});
            chainOnly.push_back({e, so});
            chainOnly.push_back({so, w});
            chainOnly.push_back({w, n});
        }
        drawLines(chainOnly, g_mod->chainColor);
    };

    if (!s_actorFetchNearby) {
        colorHolder[0] = savedColor[0]; colorHolder[1] = savedColor[1];
        colorHolder[2] = savedColor[2]; colorHolder[3] = savedColor[3];
        return;
    }

    bedrocktools::sdk::Vec3 extent = {g_mod->maxDistance, g_mod->maxDistance, g_mod->maxDistance};
    ActorVec actors = s_actorFetchNearby(g_localPlayerPtr, &extent, 1);
    if (actors.begin && actors.end) {
        for (DistanceSortedActor* it = actors.begin; it < actors.end; ++it) {
            void* ent = it->mActor;
            if (!ent) continue;
            if (ent == g_localPlayerPtr && !g_mod->showSelf) continue;
            if (s_actorIsPlayer && !s_actorIsPlayer(ent)) continue;
            if (s_actorIsInvisible && s_actorIsInvisible(ent)) continue;
            if (it->mDistance > g_mod->maxDistance) continue;
            renderTarget(ent);
        }
    }
    if (g_mod->showSelf && g_localPlayerPtr) {
        renderTarget(g_localPlayerPtr);
    }

    colorHolder[0] = savedColor[0];
    colorHolder[1] = savedColor[1];
    colorHolder[2] = savedColor[2];
    colorHolder[3] = savedColor[3];
}

TargetESPModule::TargetESPModule()
    : Module("TargetESP", "Native player target rings + orbiting chain segments (tessellator).") {
    showInMenu = true;
    g_mod = this;
}

TargetESPModule::~TargetESPModule() {
    if (g_mod == this) g_mod = nullptr;
}

void TargetESPModule::onInit() {
    uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderLevel);
    if (addr) m_patchTarget = (void*)addr;

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
        uintptr_t groupAddr = resolveADRP(reinterpret_cast<uint32_t*>(rmg), 2, 0);
        if (groupAddr)
            s_renderMaterialGroup = groupAddr + bedrocktools::sdk::offsets::MaterialGroup::mRenderMaterialGroupOffset;
    }
    if (auto aip = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsPlayer))
        s_actorIsPlayer = (Actor_isPlayer_t)aip;
    if (auto aii = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsInvisible))
        s_actorIsInvisible = (Actor_isInvisible_t)aii;
    if (auto afn = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorFetchNearbyActorsSorted))
        s_actorFetchNearby = (Actor_fetchNearbyActorsSorted_t)afn;

    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& event) { onTick(event.player); });
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
    maxDistance = j.value("maxDistance", maxDistance);
    ringHeight = j.value("ringHeight", ringHeight);
    animSpeed = j.value("animSpeed", animSpeed);
    chainLinks = j.value("chainLinks", chainLinks);
    if (j.contains("ringColor") && j["ringColor"].is_string()) {
        unsigned v = 0;
        if (sscanf(j["ringColor"].get<std::string>().c_str(), "#%x", &v) == 1) ringColor = v;
    }
    if (j.contains("chainColor") && j["chainColor"].is_string()) {
        unsigned v = 0;
        if (sscanf(j["chainColor"].get<std::string>().c_str(), "#%x", &v) == 1) chainColor = v;
    }
}

void TargetESPModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["showSelf"] = showSelf;
    j["maxDistance"] = maxDistance;
    j["ringHeight"] = ringHeight;
    j["animSpeed"] = animSpeed;
    j["chainLinks"] = chainLinks;
    char hex[16];
    snprintf(hex, sizeof(hex), "#%08X", ringColor);
    j["ringColor"] = std::string(hex);
    snprintf(hex, sizeof(hex), "#%08X", chainColor);
    j["chainColor"] = std::string(hex);
}
