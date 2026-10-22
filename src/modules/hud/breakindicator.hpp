#pragma once

#include "../Module.hpp"
#include <chrono>
#include <cstdint>

class BreakIndicatorModule : public Module {
public:
    BreakIndicatorModule();

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

private:
    void* m_gameMode = nullptr;
    bool m_breaking = false;
    float m_progress = 0.0f;
    std::chrono::steady_clock::time_point m_lastUpdate{};

    float hudPosX = 50.0f;
    float hudPosY = 50.0f;
    bool isHudModule = true;

    float m_width = 260.0f;
    float m_height = 43.0f;
    float m_textSize = 33.0f;
    float m_cornerRadius = 4.0f;

    bool m_background = true;
    float m_backgroundOpacity = 0.5f;

    uint32_t m_barColorHex = 0xFF4AE0A0;
    bool m_rainbow = false;
    float m_rainbowSpeed = 1.0f;
    float m_rainbowHue = 0.0f;

    uint32_t m_textColorHex = 0xFFFFFFFF;

    bool m_outline = true;
    uint32_t m_outlineColorHex = 0xFF202020;
    float m_outlineThickness = 1.5f;

    bool m_alwaysShow = true;
};
