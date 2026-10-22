#include "combocounter.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include <cmath>
#include <iomanip>
#include <sstream>

static ComboDisplay* g_comboDisplay = nullptr;


static void* g_localPlayerNativeCombo = nullptr;

static void onTickHookCombo(void* _this) {
    if (g_comboDisplay && g_comboDisplay->enabled) {
        g_localPlayerNativeCombo = _this;
    }
}

static bool onAttackHookCombo(void* mode, void* actor, void* a3, void* a4) {
    if (g_comboDisplay && g_comboDisplay->enabled) {
        g_comboDisplay->onAttack();
    }
    return true; 
}

static float calcTextWidthCombo(const std::string& text, float size) {
    float width = 0;
    for (char c : text) {
        if (c == 'i' || c == 'l' || c == '1' || c == ':' || c == '.' || c == ' ') width += size * 0.3f;
        else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') width += size * 0.8f;
        else width += size * 0.58f;
    }
    return width;
}

ComboDisplay::ComboDisplay()
    : Module("Combo Display", "Keeps track of consecutive hits.") {
    g_comboDisplay = this;
}

ComboDisplay::~ComboDisplay() {
    if (g_comboDisplay == this) g_comboDisplay = nullptr;
}

void ComboDisplay::onAttack() {
    auto now = std::chrono::high_resolution_clock::now();

    if (Combo < 0) {
        Combo = 1;
        m_lastHitTime = now;
        return;
    }

    if (now - m_lastHitTime > std::chrono::milliseconds(480)) {
        Combo++;
        m_lastHitTime = now;
    }
}

void ComboDisplay::onHurt() {
    if (Combo > 0) Combo = 0;
    if (allowNegatives) Combo--;
}

void ComboDisplay::onInit() {
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { onTickHookCombo(event.player); });
    
    bedrocktools::events::bus().subscribe<bedrocktools::events::AttackEvent>([](auto& event) {
        if (!onAttackHookCombo(event.gameMode, event.target, event.argument2, event.argument3)) event.cancel();
    });
}

void ComboDisplay::onEnable() {
}

void ComboDisplay::onDisable() {
    Combo = 0;
}

void ComboDisplay::onFrame() {
    if (!enabled) return;
    
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = now - m_lastHitTime;
    
    if (duration.count() >= 15.0) {
        Combo = 0;
    }
    
    if (g_localPlayerNativeCombo) {
        int currentHurtTime = static_cast<bedrocktools::sdk::Player*>(g_localPlayerNativeCombo)->hurtTime();
        static int lastHurtTime = 0;
        
        if (currentHurtTime > 0 && lastHurtTime == 0) {
            onHurt();
        }
        lastHurtTime = currentHurtTime;
    }
    
    if (!allowNegatives && Combo < 0) {
        Combo = 0;
    }

    if (Combo == 0) return;

    std::string text = std::string("Combo: ") + std::to_string(Combo);

    std::vector<PLModMenu_DrawCommand> cmds;
    float boxW = calcTextWidthCombo(text, m_size) + 12.0f;
    float boxH = m_size + 8.0f;
    
    
    if (hudPosX < 0.f) hudPosX = 20.f;
    if (hudPosY < 0.f) hudPosY = 200.f;
    
    float boxX = hudPosX;
    float boxY = hudPosY;

    if (showBackground) {
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

void ComboDisplay::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();
    if (j.contains("m_size")) m_size = j["m_size"].get<float>();
    if (j.contains("showBackground")) showBackground = j["showBackground"].get<bool>();
    if (j.contains("m_backgroundOpacity")) m_backgroundOpacity = j["m_backgroundOpacity"].get<float>();
    if (j.contains("allowNegatives")) allowNegatives = j["allowNegatives"].get<bool>();
}

void ComboDisplay::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;
    j["m_size"] = m_size;
    j["showBackground"] = showBackground;
    j["m_backgroundOpacity"] = m_backgroundOpacity;
    j["allowNegatives"] = allowNegatives;
}
