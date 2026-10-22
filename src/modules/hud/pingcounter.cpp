#include "pingcounter.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include "core/memory/Hooks.hpp"
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <chrono>

static void (*_update_orig)(void* _this);
static PingCounterModule* g_pingMod = nullptr;
static auto g_last_update_time = std::chrono::steady_clock::now();

static void _update_hook(void* _this) {
    if (_update_orig) {
        _update_orig(_this);
    }
    
    if (g_pingMod && g_pingMod->enabled) {
        int avgPing = *(int*)((uintptr_t)_this + bedrocktools::sdk::offsets::RakNetConnector::mAvgPing);
        if (avgPing >= 0) {
            g_pingMod->m_ping = avgPing;
            g_last_update_time = std::chrono::steady_clock::now();
        }
    }
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

PingCounterModule::PingCounterModule() 
    : Module("Ping Counter", "Displays the server ping on screen.") {
    m_patched = false;
    m_patchTarget = nullptr;
    g_pingMod = this;
}

PingCounterModule::~PingCounterModule() {
    if (g_pingMod == this) g_pingMod = nullptr;
}

void PingCounterModule::onInit() {
    if (m_patchTarget) return;
    uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RaknetUpdate);
    if (addr != 0) {
        m_patchTarget = (void*)addr;
    }
}

void PingCounterModule::applyPatch() {
    if (m_patched || !m_patchTarget) return;
    bedrocktools::hooks::install(m_patchTarget, (void*)_update_hook, (void**)&_update_orig);
    m_patched = true;
}

void PingCounterModule::onEnable() {
    applyPatch();
}

void PingCounterModule::onDisable() {
}

void PingCounterModule::onFrame() {
    if (!enabled) return;

    std::string text = "Ping: " + std::to_string(m_ping) + " ms";

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
    txtCmd.x = boxX; 
    txtCmd.y = boxY;
    txtCmd.w = boxW;
    txtCmd.h = boxH;
    txtCmd.color = 0xFFFFFFFF; 
    txtCmd.size = m_size;
    txtCmd.text = text.c_str();
    cmds.push_back(txtCmd);

    submitDrawCommands(moduleId, cmds);
}

void PingCounterModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();
    if (j.contains("m_size")) m_size = j["m_size"].get<float>();
    if (j.contains("m_background")) m_background = j["m_background"].get<bool>();
    if (j.contains("m_backgroundOpacity")) m_backgroundOpacity = j["m_backgroundOpacity"].get<float>();
}

void PingCounterModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;
    j["m_size"] = m_size;
    j["m_background"] = m_background;
    j["m_backgroundOpacity"] = m_backgroundOpacity;
}
