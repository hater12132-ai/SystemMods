#include "reachcounter.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include <cmath>
#include <iomanip>
#include <sstream>

static ReachCounterModule* g_reachCounterMod = nullptr;


using LevelGetHitResultFn = void*(*)(void*);
static LevelGetHitResultFn getHitResultFn = nullptr;

static void* g_localPlayerNative = nullptr;

static void onTickHook(void* _this) {
    if (g_reachCounterMod && g_reachCounterMod->enabled) {
        g_localPlayerNative = _this;
    }
}

static bool onAttackHook(void* mode, void* actor, void* a3, void* a4) {
    if (g_reachCounterMod && g_reachCounterMod->enabled) {
        if (g_localPlayerNative) {
            void* level_ptr = *(void**)((uintptr_t)g_localPlayerNative + bedrocktools::sdk::offsets::Actor::mLevel);
            if (level_ptr && getHitResultFn) {
                void* hit = getHitResultFn(level_ptr);
                if (hit) {
                    int mType = *(int*)((uintptr_t)hit + bedrocktools::sdk::offsets::HitResult::mType);
                    if (mType == 0 || mType == 1) { 
                        bedrocktools::sdk::Vec3 startPos = *(bedrocktools::sdk::Vec3*)((uintptr_t)hit + bedrocktools::sdk::offsets::HitResult::mStartPos);
                        bedrocktools::sdk::Vec3 pos = *(bedrocktools::sdk::Vec3*)((uintptr_t)hit + bedrocktools::sdk::offsets::HitResult::mPos);
                        
                        float dx = startPos.x - pos.x;
                        float dy = startPos.y - pos.y;
                        float dz = startPos.z - pos.z;
                        float reach = std::sqrt(dx*dx + dy*dy + dz*dz);
                        g_reachCounterMod->updateReach(reach);
                    }
                }
            }
        }
    }
    return true; 
}

static float calcTextWidth(const std::string& text, float size) {
    float width = 0;
    for (char c : text) {
        if (c == 'i' || c == 'l' || c == '1' || c == ':' || c == '.' || c == ' ') width += size * 0.3f;
        else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') width += size * 0.8f;
        else width += size * 0.58f;
    }
    return width;
}


ReachCounterModule::ReachCounterModule()
    : Module("Reach Counter", "Displays your attack reach distance on screen.") {
    g_reachCounterMod = this;
}

ReachCounterModule::~ReachCounterModule() {
    if (g_reachCounterMod == this) g_reachCounterMod = nullptr;
}


void ReachCounterModule::updateReach(float reach) {
    m_currentReach = reach;
    m_lastHitTime = std::chrono::high_resolution_clock::now();
}

void ReachCounterModule::onInit() {
    
    uintptr_t getHitResultAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::LevelGetHitResult);
    if (getHitResultAddr) {
        getHitResultFn = reinterpret_cast<LevelGetHitResultFn>(getHitResultAddr);
    }
    
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { onTickHook(event.player); });
    
    bedrocktools::events::bus().subscribe<bedrocktools::events::AttackEvent>([](auto& event) {
        if (!onAttackHook(event.gameMode, event.target, event.argument2, event.argument3)) event.cancel();
    });
}

void ReachCounterModule::onEnable() {
}

void ReachCounterModule::onDisable() {
    m_currentReach = 0.0f;
}

void ReachCounterModule::onFrame() {
    if (!enabled) return;
    
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = now - m_lastHitTime;
    
    if (duration.count() >= 15.0) {
        m_currentReach = 0.0f;
    }
    
    if (m_currentReach <= 0.0f) return;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << m_currentReach;
    std::string text = std::string("Reach: ") + oss.str();

    std::vector<PLModMenu_DrawCommand> cmds;
    float boxW = calcTextWidth(text, m_size) + 12.0f;
    float boxH = m_size + 8.0f;
    float boxX = hudPosX;
    float boxY = hudPosY;

    if (m_background) {
        PLModMenu_DrawCommand bgCmd = {};
        bgCmd.type = PL_DRAW_RECT_FILLED;
        bgCmd.x = boxX;
        bgCmd.y = boxY;
        bgCmd.w = boxW;
        bgCmd.h = boxH;
        int alpha = (int)(m_backgroundOpacity * 255.0f);
        bgCmd.color = (alpha << 24) | 0x000000;
        cmds.push_back(bgCmd);
    }

    PLModMenu_DrawCommand txtCmd = {};
    txtCmd.type = PL_DRAW_TEXT;
    txtCmd.x = boxX + 6.0f;
    txtCmd.y = boxY + 4.0f;
    txtCmd.w = boxW;
    txtCmd.h = boxH;
    txtCmd.color = 0xFFFFFFFF; 
    txtCmd.size = m_size;
    txtCmd.text = text.c_str();
    cmds.push_back(txtCmd);

    submitDrawCommands(moduleId, cmds);
}

void ReachCounterModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();
    if (j.contains("m_size")) m_size = j["m_size"].get<float>();
    if (j.contains("m_background")) m_background = j["m_background"].get<bool>();
    if (j.contains("m_backgroundOpacity")) m_backgroundOpacity = j["m_backgroundOpacity"].get<float>();
}

void ReachCounterModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;
    j["m_size"] = m_size;
    j["m_background"] = m_background;
    j["m_backgroundOpacity"] = m_backgroundOpacity;
}
