#include "speeddisplay.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>
#include <cmath>

static SpeedDisplayModule* g_speedMod = nullptr;

static void s_speedCallback(bedrocktools::sdk::Player* player) {
    if (!g_speedMod || !g_speedMod->enabled || !player) return;
    g_speedMod->updatePosition(player->position());
}

static float calcTextWidth(const std::string& text, float size) {
    float width = 0;
    for (char c : text) {
        if (c == 'i' || c == 'l' || c == '1' || c == ':' || c == '.' || c == ' ') width += size * 0.3f;
        else if (c == 'm' || c == 'w' || c == 'M' || c == 'W' || c == '/') width += size * 0.8f;
        else width += size * 0.58f;
    }
    return width;
}

SpeedDisplayModule::SpeedDisplayModule()
    : Module("Speed Display", "Displays your speed in m/s.") {
    m_currentSpeed = 0.0f;
    m_use3D = false;
    m_firstTick = true;
    m_lastPos = {0.f, 0.f, 0.f};
    g_speedMod = this;
}

SpeedDisplayModule::~SpeedDisplayModule() {
    if (g_speedMod == this) g_speedMod = nullptr;
}

void SpeedDisplayModule::updatePosition(const bedrocktools::sdk::Vec3& pos) {
    if (m_firstTick) {
        m_lastPos = pos;
        m_firstTick = false;
        m_currentSpeed = 0.0f;
        return;
    }

    float dx = pos.x - m_lastPos.x;
    float dy = pos.y - m_lastPos.y;
    float dz = pos.z - m_lastPos.z;

    float distSq = 0.0f;
    if (m_use3D) {
        distSq = (dx * dx) + (dy * dy) + (dz * dz);
    } else {
        distSq = (dx * dx) + (dz * dz);
    }

    if (distSq > 100.0f) {
        m_lastPos = pos;
        return; 
    }

    float instSpeed = std::sqrt(distSq) * 20.0f; 

    m_currentSpeed = (m_currentSpeed * 0.8f) + (instSpeed * 0.2f);

    m_lastPos = pos;
}

void SpeedDisplayModule::onInit() {
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { s_speedCallback(event.player); });
}

void SpeedDisplayModule::onEnable() {
    m_firstTick = true;
    m_currentSpeed = 0.0f;
}

void SpeedDisplayModule::onDisable() {
    m_firstTick = true;
    m_currentSpeed = 0.0f;
}

void SpeedDisplayModule::onFrame() {
    if (!enabled) return;

    std::string text = "Speed: " + std::to_string((int)m_currentSpeed) + " m/s";

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

void SpeedDisplayModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();
    if (j.contains("m_size")) m_size = j["m_size"].get<float>();
    if (j.contains("m_background")) m_background = j["m_background"].get<bool>();
    if (j.contains("m_backgroundOpacity")) m_backgroundOpacity = j["m_backgroundOpacity"].get<float>();
}

void SpeedDisplayModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;
    j["m_size"] = m_size;
    j["m_background"] = m_background;
    j["m_backgroundOpacity"] = m_backgroundOpacity;
}
