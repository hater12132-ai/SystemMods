#include "playercoords.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>

static PlayerCoordsModule* g_coordsMod = nullptr;

static void s_coordsCallback(bedrocktools::sdk::Player* player) {
    if (!g_coordsMod || !g_coordsMod->enabled || !player) return;
    g_coordsMod->updateCoords(player->position());
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


PlayerCoordsModule::PlayerCoordsModule()
    : Module("Player Coords", "Displays your XYZ coordinates on screen.") {
    m_currentPos = {0.f, 0.f, 0.f};
    g_coordsMod = this;
}

PlayerCoordsModule::~PlayerCoordsModule() {
    if (g_coordsMod == this) g_coordsMod = nullptr;
}


void PlayerCoordsModule::updateCoords(const bedrocktools::sdk::Vec3& pos) {
    m_currentPos = pos;
}

void PlayerCoordsModule::onInit() {
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { s_coordsCallback(event.player); });
}

void PlayerCoordsModule::onEnable() {
}

void PlayerCoordsModule::onDisable() {
    m_currentPos = {0.f, 0.f, 0.f};
}

void PlayerCoordsModule::onFrame() {
    if (!enabled) return;

    std::string text = "X: " + std::to_string((int)m_currentPos.x) +
                       " Y: " + std::to_string((int)m_currentPos.y) +
                       " Z: " + std::to_string((int)m_currentPos.z);

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

void PlayerCoordsModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();
    if (j.contains("m_size")) m_size = j["m_size"].get<float>();
    if (j.contains("m_background")) m_background = j["m_background"].get<bool>();
    if (j.contains("m_backgroundOpacity")) m_backgroundOpacity = j["m_backgroundOpacity"].get<float>();
}

void PlayerCoordsModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;
    j["m_size"] = m_size;
    j["m_background"] = m_background;
    j["m_backgroundOpacity"] = m_backgroundOpacity;
}
