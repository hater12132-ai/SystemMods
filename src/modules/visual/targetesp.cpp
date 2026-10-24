#include "targetesp.hpp"
#include "fakeplayer_shared.hpp"
#include "render_dispatch.hpp"
#include "tess_util.hpp"
#include "sm_status.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <cmath>
#include <algorithm>
#include <vector>

typedef bool (*Actor_isPlayer_t)(void*);
typedef bool (*Actor_isInvisible_t)(void*);
struct DistanceSortedActor { void* mActor; float mDistance; float _pad; };
struct ActorVec { DistanceSortedActor* begin; DistanceSortedActor* end; DistanceSortedActor* cap; };
typedef ActorVec (*Actor_fetchNearbyActorsSorted_t)(void*, void*, int);

static TargetESPModule* g_mod = nullptr;
static Actor_isPlayer_t s_isPlayer = nullptr;
static Actor_isInvisible_t s_isInvis = nullptr;
static Actor_fetchNearbyActorsSorted_t s_fetch = nullptr;
static void* g_lp = nullptr;
static bedrocktools::sdk::Vec3 g_pos{};
static float g_phase = 0.f;
static int g_frames = 0;

struct AABB { bedrocktools::sdk::Vec3 min, max; };

static AABB getAABB(void* actor) {
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

static void onTick(void* player) {
    if (!g_mod || !g_mod->enabled) return;
    g_lp = player;
    uintptr_t svc = *(uintptr_t*)((uintptr_t)player + bedrocktools::sdk::offsets::Actor::mStateVectorComponent);
    if (svc) g_pos = *(bedrocktools::sdk::Vec3*)svc;
    g_phase += 0.016f * g_mod->animSpeed;
}

static void buildStyle(tess_util::Lines& A, tess_util::Lines& B,
    float cx, float cz, float y0, float y2, float rx, float rz, int style) {
    using tess_util::Context;
    float mid = (y0 + y2) * 0.5f;
    float spin = g_phase;

    auto diamond = [&](tess_util::Lines& L, float x, float y, float z, float s) {
        L.push_back({{x,y+s,z},{x+s,y,z}});
        L.push_back({{x+s,y,z},{x,y-s,z}});
        L.push_back({{x,y-s,z},{x-s,y,z}});
        L.push_back({{x-s,y,z},{x,y+s,z}});
    };

    switch (style) {
    case 1: // Nova
        Context::addRing(A, cx, cz, mid, rx*0.3f, rz*0.3f, 24, spin);
        for (int i=0;i<16;i++) {
            float a = spin*0.3f + (float)(2*M_PI*i/16);
            float c=std::cos(a), s=std::sin(a);
            B.push_back({{cx+c*rx*0.25f, mid, cz+s*rz*0.25f},
                         {cx+c*rx*1.4f, mid, cz+s*rz*1.4f}});
        }
        break;
    case 3: // Rhombus
        for (int layer=0; layer<3; layer++) {
            float sc = 1.f - layer*0.2f;
            float ang = spin*0.7f + layer*0.4f;
            bedrocktools::sdk::Vec3 p[4];
            for (int i=0;i<4;i++) {
                float a = ang + (float)(M_PI*0.5*i);
                p[i] = {cx+std::cos(a)*rx*1.1f*sc, mid+(layer-1)*0.3f, cz+std::sin(a)*rz*1.1f*sc};
            }
            for (int i=0;i<4;i++) (layer?B:A).push_back({p[i], p[(i+1)&3]});
        }
        break;
    case 4: // Scanner
        Context::addRing(A, cx, cz, y0, rx, rz, 36, 0);
        Context::addRing(A, cx, cz, y2, rx*0.7f, rz*0.7f, 28, 0);
        {
            float t = 0.5f + 0.5f*std::sin(spin*2.f);
            Context::addRing(B, cx, cz, y0+(y2-y0)*t, rx*1.15f, rz*1.15f, 40, spin);
        }
        break;
    case 6: // Swords
        Context::addRing(A, cx, cz, y0, rx*0.5f, rz*0.5f, 20, spin);
        for (int i=0;i<6;i++) {
            float a = spin*0.9f + (float)(2*M_PI*i/6);
            float c=std::cos(a), s=std::sin(a);
            B.push_back({{cx+c*rx*0.3f, mid-0.2f, cz+s*rz*0.3f},
                         {cx+c*rx*1.3f, mid+0.4f, cz+s*rz*1.3f}});
        }
        break;
    case 7: // Crystals
        Context::addRing(A, cx, cz, y0, rx, rz, 28, spin*0.2f);
        for (int i=0;i<8;i++) {
            float a = spin*0.8f + (float)(2*M_PI*i/8);
            float elev = y0 + (y2-y0)*(0.3f+0.4f*(i/8.f));
            float ox = cx+std::cos(a)*rx*1.1f, oz = cz+std::sin(a)*rz*1.1f;
            float h = 0.25f;
            B.push_back({{ox, elev+h, oz},{ox+0.12f, elev-h*0.3f, oz}});
            B.push_back({{ox, elev+h, oz},{ox-0.06f, elev-h*0.3f, oz+0.1f}});
            B.push_back({{ox, elev+h, oz},{ox-0.06f, elev-h*0.3f, oz-0.1f}});
        }
        break;
    case 11: // Helix
        Context::addRing(A, cx, cz, y0, rx*0.9f, rz*0.9f, 28, spin);
        for (int strand=0; strand<2; strand++) {
            float off = strand*(float)M_PI;
            for (int i=0;i<40;i++) {
                float t0=i/40.f, t1=(i+1)/40.f;
                float a0=spin*1.5f+t0*4.f*(float)M_PI+off;
                float a1=spin*1.5f+t1*4.f*(float)M_PI+off;
                (strand?B:A).push_back({
                    {cx+std::cos(a0)*rx, y0+(y2-y0)*t0, cz+std::sin(a0)*rz},
                    {cx+std::cos(a1)*rx, y0+(y2-y0)*t1, cz+std::sin(a1)*rz}});
            }
        }
        break;
    default: // Chains
        Context::addRing(A, cx, cz, y0, rx, rz, 48, spin*0.4f);
        Context::addRing(A, cx, cz, mid, rx*0.85f, rz*0.85f, 36, -spin*0.3f);
        Context::addRing(A, cx, cz, y2, rx*0.5f, rz*0.5f, 24, spin*0.5f);
        for (int i=0;i<std::clamp(g_mod?g_mod->chainLinks:10,4,24);i++) {
            float base = spin*1.3f + (float)(2*M_PI*i/10);
            float elev = y0 + (y2-y0)*(0.2f+0.6f*(0.5f+0.5f*std::sin(base*1.7f)));
            float ox = cx+std::cos(base)*rx*1.15f, oz = cz+std::sin(base)*rz*1.15f;
            diamond(B, ox, elev, oz, 0.12f);
        }
        break;
    }
}

static void drawAround(float cx, float cz, float y0, float y2, float rx, float rz,
    void* tess, void* sc, void* mat, float camX, float camY, float camZ) {
    if (!g_mod) return;
    tess_util::Lines A, B;
    A.reserve(200); B.reserve(200);
    float pulse = 1.f + 0.06f*std::sin(g_phase*2.4f);
    rx *= pulse; rz *= pulse;
    buildStyle(A, B, cx, cz, y0, y2, rx, rz, std::clamp(g_mod->style, 0, 12));
    auto& T = tess_util::ctx();
    T.drawLines(tess, sc, mat, camX, camY, camZ, A, g_mod->ringColor);
    T.drawLines(tess, sc, mat, camX, camY, camZ, B, g_mod->chainColor);
}

static void onRender(void* levelRenderer, void* screenContext, void* /*a3*/) {
    if (!g_mod || !g_mod->enabled) return;
    if (!screenContext || (uintptr_t)screenContext < 0x1000) return;

    auto& T = tess_util::ctx();
    if (!T.ready()) return;

    uintptr_t tessPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mTessellator);
    if (!tessPtr || tessPtr < 0x1000) return;
    void* tess = (void*)tessPtr;
    uintptr_t lrp = *(uintptr_t*)((uintptr_t)levelRenderer + bedrocktools::sdk::offsets::LevelRenderer::mLevelRendererPlayer);
    if (!lrp || lrp < 0x1000) return;
    float camX = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos);
    float camY = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 4);
    float camZ = *(float*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mCamPos + 8);

    void* mat = (void*)(lrp + bedrocktools::sdk::offsets::LevelRendererPlayer::mSelectionOverlayMaterial);

    uintptr_t chPtr = *(uintptr_t*)((uintptr_t)screenContext + bedrocktools::sdk::offsets::ScreenContext::mColorHolder);
    if (!chPtr || chPtr < 0x1000) return;
    float* ch = (float*)chPtr;
    float saved[4] = {ch[0],ch[1],ch[2],ch[3]};
    ch[0]=ch[1]=ch[2]=ch[3]=1.f;

    int drawn = 0;

    // Real players
    if (s_fetch && g_lp) {
        bedrocktools::sdk::Vec3 extent{g_mod->maxDistance, g_mod->maxDistance, g_mod->maxDistance};
        ActorVec actors = s_fetch(g_lp, &extent, 1);
        if (actors.begin && actors.end) {
            for (auto* it = actors.begin; it < actors.end; ++it) {
                void* ent = it->mActor;
                if (!ent) continue;
                if (ent == g_lp && !g_mod->showSelf) continue;
                if (ent != g_lp) {
                    if (s_isPlayer && !s_isPlayer(ent)) continue;
                    if (s_isInvis && s_isInvis(ent)) continue;
                }
                if (it->mDistance > g_mod->maxDistance) continue;
                AABB aabb = getAABB(ent);
                float cx = (aabb.min.x+aabb.max.x)*0.5f;
                float cz = (aabb.min.z+aabb.max.z)*0.5f;
                float y0 = aabb.min.y + g_mod->ringHeight;
                float y2 = aabb.max.y - 0.04f;
                if (y2 <= y0) y2 = y0 + 1.6f;
                float rx = (aabb.max.x-aabb.min.x)*0.55f + 0.18f;
                float rz = (aabb.max.z-aabb.min.z)*0.55f + 0.18f;
                drawAround(cx, cz, y0, y2, rx, rz, tess, screenContext, mat, camX, camY, camZ);
                drawn++;
            }
        }
    }

    // Always draw on self if showSelf and we have pos even without fetch entry
    if (g_mod->showSelf && g_lp) {
        AABB aabb = getAABB(g_lp);
        // if AABB invalid, invent one at player feet
        if (aabb.max.y <= aabb.min.y) {
            aabb.min = {g_pos.x-0.3f, g_pos.y, g_pos.z-0.3f};
            aabb.max = {g_pos.x+0.3f, g_pos.y+1.8f, g_pos.z+0.3f};
        }
        float cx = (aabb.min.x+aabb.max.x)*0.5f;
        float cz = (aabb.min.z+aabb.max.z)*0.5f;
        float y0 = aabb.min.y + g_mod->ringHeight;
        float y2 = std::max(aabb.max.y - 0.04f, y0 + 1.6f);
        float rx = (aabb.max.x-aabb.min.x)*0.55f + 0.18f;
        float rz = (aabb.max.z-aabb.min.z)*0.55f + 0.18f;
        drawAround(cx, cz, y0, y2, rx, rz, tess, screenContext, mat, camX, camY, camZ);
        drawn++;
    }

    // FakePlayer dummy
    if (g_mod->includeFakePlayer) {
        auto& fp = fakeplayer::get();
        if (fp.active) {
            float cx = fp.feet.x, cz = fp.feet.z;
            float y0 = fp.feet.y + g_mod->ringHeight;
            float y2 = fp.feet.y + fp.height - 0.04f;
            float rx = fp.width*0.55f + 0.18f;
            drawAround(cx, cz, y0, y2, rx, rx, tess, screenContext, mat, camX, camY, camZ);
            drawn++;
        }
    }

    ch[0]=saved[0]; ch[1]=saved[1]; ch[2]=saved[2]; ch[3]=saved[3];

    if ((++g_frames % 120) == 1) {
        sm_status::write("TargetESP: frames=%d drawn=%d style=%d fetch=%p lp=%p",
            g_frames, drawn, g_mod->style, (void*)s_fetch, g_lp);
    }
}

TargetESPModule::TargetESPModule()
    : Module("TargetESP", "Player rings/chains ESP (13 styles). showSelf ON to verify.") {
    showInMenu = true;
    g_mod = this;
}
TargetESPModule::~TargetESPModule() { if (g_mod == this) g_mod = nullptr; }

void TargetESPModule::onInit() {
    sm_status::write("TargetESP: onInit");
    tess_util::ctx().resolve();
    if (auto a = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsPlayer))
        s_isPlayer = (Actor_isPlayer_t)a;
    if (auto a = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsInvisible))
        s_isInvis = (Actor_isInvisible_t)a;
    if (auto a = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorFetchNearbyActorsSorted))
        s_fetch = (Actor_fetchNearbyActorsSorted_t)a;
    sm_status::write("TargetESP: isPlayer=%p isInvis=%p fetch=%p",
        (void*)s_isPlayer, (void*)s_isInvis, (void*)s_fetch);
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& e) { onTick(e.player); });
    render_dispatch::add(onRender);
}

void TargetESPModule::onEnable() {
    sm_status::write("TargetESP: onEnable style=%d", style);
    render_dispatch::ensureHooked();
}
void TargetESPModule::onDisable() {
    sm_status::write("TargetESP: onDisable");
}

void TargetESPModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    showSelf = j.value("showSelf", showSelf);
    onlyLookingAt = j.value("onlyLookingAt", onlyLookingAt);
    includeFakePlayer = j.value("includeFakePlayer", includeFakePlayer);
    maxDistance = j.value("maxDistance", maxDistance);
    ringHeight = j.value("ringHeight", ringHeight);
    animSpeed = j.value("animSpeed", animSpeed);
    thickness = j.value("thickness", thickness);
    chainLinks = j.value("chainLinks", chainLinks);
    style = std::clamp(j.value("style", style), 0, (int)StyleCount - 1);
    auto parse = [](const nlohmann::json& j, const char* k, uint32_t& o) {
        if (j.contains(k) && j[k].is_string()) {
            unsigned v=0; if (sscanf(j[k].get<std::string>().c_str(), "#%x", &v)==1) o=v;
        }
    };
    parse(j, "ringColor", ringColor);
    parse(j, "chainColor", chainColor);
}

void TargetESPModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["showSelf"]=showSelf; j["onlyLookingAt"]=onlyLookingAt;
    j["includeFakePlayer"]=includeFakePlayer; j["maxDistance"]=maxDistance;
    j["ringHeight"]=ringHeight; j["animSpeed"]=animSpeed;
    j["thickness"]=thickness; j["chainLinks"]=chainLinks; j["style"]=style;
    char hex[16];
    snprintf(hex,sizeof(hex),"#%08X",ringColor); j["ringColor"]=std::string(hex);
    snprintf(hex,sizeof(hex),"#%08X",chainColor); j["chainColor"]=std::string(hex);
}
