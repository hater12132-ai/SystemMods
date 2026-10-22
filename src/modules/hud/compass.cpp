#include "compass.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>

#include <cmath>
#include <cstdio>

static CompassModule* g_compassMod = nullptr;

static void s_compassCallback(bedrocktools::sdk::Player* player) {
    if (!g_compassMod || !g_compassMod->enabled || !player) return;
    const auto rotation = player->rotation();
    g_compassMod->m_pitch = rotation.x;
    g_compassMod->m_yaw = rotation.y;
}

CompassModule::CompassModule()
    : Module("Compass", "Linear HUD compass showing your facing direction.") {
    g_compassMod = this;
}

CompassModule::~CompassModule() {
    if (g_compassMod == this) g_compassMod = nullptr;
}

void CompassModule::onInit() {
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { s_compassCallback(event.player); });
}

void CompassModule::onEnable() {}

void CompassModule::onDisable() {}

static const char* cardinalLabel(int deg) {
    while (deg <   0) deg += 360;
    while (deg >= 360) deg -= 360;
    if (deg ==   0) return "N";
    if (deg ==  45) return "NE";
    if (deg ==  90) return "E";
    if (deg == 135) return "SE";
    if (deg == 180) return "S";
    if (deg == 225) return "SW";
    if (deg == 270) return "W";
    if (deg == 315) return "NW";
    return nullptr;
}

void CompassModule::onFrame() {
    if (!enabled) return;

    std::vector<PLModMenu_DrawCommand> cmds;

    const float sc    = m_scale;
    const float barW  = m_barWidth  * sc;
    const float barH  = m_barHeight * sc;
    
    if (hudPosX < 0.f) hudPosX = 100.f; 
    if (hudPosY < 0.f) hudPosY = 100.f; 

    const float barX  = hudPosX;
    const float barY  = hudPosY;                   

    const float cx    = barX + barW * 0.5f;   
    const float midY  = barY + barH * 0.5f;

    uint32_t bgCol, fgCol, fgDim, cardCol;
    
    bgCol = (uint32_t)(m_opacity * 140) << 24; 
    fgCol = ((uint32_t)(m_opacity * 255) << 24) | ((uint32_t)(m_colorR * 255) << 16) | ((uint32_t)(m_colorG * 255) << 8) | (uint32_t)(m_colorB * 255);
    fgDim = ((uint32_t)(m_opacity * 180) << 24) | ((uint32_t)(m_colorR * 180) << 16) | ((uint32_t)(m_colorG * 180) << 8) | (uint32_t)(m_colorB * 180);
    cardCol = ((uint32_t)(m_opacity * 255) << 24) | (0xFF << 16) | ((uint32_t)(m_colorG * 200) << 8) | (uint32_t)(m_colorB * 100);

    PLModMenu_DrawCommand bgCmd = {};
    bgCmd.type = PL_DRAW_RECT_FILLED;
    bgCmd.x = barX;
    bgCmd.y = barY;
    bgCmd.w = barW;
    bgCmd.h = barH;
    bgCmd.color = bgCol;
    cmds.push_back(bgCmd);

    float delta = m_yaw - m_animYaw;
    while (delta > 180.f) delta -= 360.f;
    while (delta < -180.f) delta += 360.f;
    float blend = 1.0f - std::exp(-15.0f * (1.f/60.f)); 
    m_animYaw += delta * blend;

    float rawYaw = m_animYaw;  

    float bearing = fmodf(rawYaw + 180.f, 360.f);
    if (bearing < 0.f) bearing += 360.f;

    const float degStep = 15.f;
    const float pxPerDeg = (barW * 0.5f) / m_range;

    float leftBearing = bearing - m_range;
    float firstTick   = ceilf(leftBearing / degStep) * degStep;

    float fontSzLabel    = 11.f * sc * 2.5f; 
    float fontSzCardinal = 13.f * sc * 2.5f;

    auto applyAlpha = [](uint32_t col, float alpha) -> uint32_t {
        uint32_t a = (col >> 24) & 0xFF;
        return (col & 0x00FFFFFF) | ((uint32_t)(a * alpha) << 24);
    };

    for (float tick = firstTick; tick <= bearing + m_range + degStep; tick += degStep) {
        float screenX = cx + (tick - bearing) * pxPerDeg;
        if (screenX < barX || screenX > barX + barW) continue;

        float distFromCenter = std::abs(screenX - cx);
        float maxDist = barW * 0.5f;
        float alphaFade = 1.0f - std::pow(distFromCenter / maxDist, 1.5f);
        if (alphaFade < 0.0f) alphaFade = 0.0f;

        int deg = (int)fmodf(tick, 360.f);
        while (deg <   0) deg += 360;
        while (deg >= 360) deg -= 360;

        float majorMult = ((deg % 90) == 0) ? 0.70f : ((deg % 45) == 0) ? 0.55f : 0.40f;
        float tickH = barH * majorMult;
        float tickY0 = midY - tickH * 0.5f;
        float tickY1 = midY + tickH * 0.5f;

        uint32_t tickCol = ((deg % 90) == 0) ? fgCol : fgDim;
        
        PLModMenu_DrawCommand lineCmd = {};
        lineCmd.type = PL_DRAW_LINE;
        lineCmd.x = screenX;
        lineCmd.y = tickY0;
        lineCmd.w = 0;
        lineCmd.h = tickY1 - tickY0;
        lineCmd.color = applyAlpha(tickCol, alphaFade);
        lineCmd.size = 1.5f * sc;
        cmds.push_back(lineCmd);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", deg);
        
        PLModMenu_DrawCommand labelCmd = {};
        labelCmd.type = PL_DRAW_TEXT;
        labelCmd.x = screenX; 
        
        labelCmd.y = tickY1 + 5.f * sc;
        labelCmd.w = 1.f;
        labelCmd.h = 1.f;
        labelCmd.color = applyAlpha(fgDim, alphaFade);
        labelCmd.size = fontSzLabel;
        
        
        
        labelCmd.text = buf;
        cmds.push_back(labelCmd);

        const char* card = ((deg % 45) == 0) ? cardinalLabel(deg) : nullptr;
        if (card) {
            PLModMenu_DrawCommand cardCmd = {};
            cardCmd.type = PL_DRAW_TEXT;
            cardCmd.x = screenX;
            cardCmd.y = tickY0 - 15.f * sc;
            cardCmd.w = 1.f;
            cardCmd.h = 1.f;
            cardCmd.color = applyAlpha(cardCol, alphaFade);
            cardCmd.size = fontSzCardinal;
            cardCmd.text = card;
            cmds.push_back(cardCmd);
        }
    }

    float ptrHalf = 5.f * sc;
    float ptrY    = barY;
    
    PLModMenu_DrawCommand ptrCmd = {};
    ptrCmd.type = PL_DRAW_TRIANGLE_FILLED;
    ptrCmd.x = cx - ptrHalf;
    ptrCmd.y = ptrY;
    ptrCmd.w = cx + ptrHalf;
    ptrCmd.h = ptrY;
    ptrCmd.x3 = cx;
    ptrCmd.y3 = ptrY + ptrHalf * 1.4f;
    ptrCmd.color = fgCol;
    cmds.push_back(ptrCmd);

    char bearBuf[16];
    snprintf(bearBuf, sizeof(bearBuf), "%.1f", bearing);
    float readSz = 11.f * sc * 2.5f;
    
    PLModMenu_DrawCommand readCmd = {};
    readCmd.type = PL_DRAW_TEXT;
    readCmd.x = cx;
    readCmd.y = barY + barH + 5.f * sc;
    readCmd.w = 1.f;
    readCmd.h = 1.f;
    readCmd.color = fgCol;
    readCmd.size = readSz;
    readCmd.text = bearBuf;
    cmds.push_back(readCmd);

    submitDrawCommands(moduleId, cmds);
}

void CompassModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();
    if (j.contains("barWidth"))  m_barWidth  = j["barWidth"].get<float>();
    if (j.contains("barHeight")) m_barHeight = j["barHeight"].get<float>();
    if (j.contains("scale"))     m_scale     = j["scale"].get<float>();
    if (j.contains("range"))     m_range     = j["range"].get<float>();
    if (j.contains("opacity"))   m_opacity   = j["opacity"].get<float>();
    if (j.contains("colorR"))    m_colorR    = j["colorR"].get<float>();
    if (j.contains("colorG"))    m_colorG    = j["colorG"].get<float>();
    if (j.contains("colorB"))    m_colorB    = j["colorB"].get<float>();
}

void CompassModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;
    j["barWidth"]  = m_barWidth;
    j["barHeight"] = m_barHeight;
    j["scale"]     = m_scale;
    j["range"]     = m_range;
    j["opacity"]   = m_opacity;
    j["colorR"]    = m_colorR;
    j["colorG"]    = m_colorG;
    j["colorB"]    = m_colorB;
}
