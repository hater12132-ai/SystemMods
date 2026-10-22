#pragma once

#include "../Module.hpp"
#include <chrono>

class ComboDisplay : public Module {
public:
    ComboDisplay();
    ~ComboDisplay() override;
    
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;
    
    void onAttack();
    void onHurt();

private:
    int Combo = 0;
    std::chrono::high_resolution_clock::time_point m_lastHitTime;

    float hudPosX = -1.0f;
    float hudPosY = -1.0f;
    bool isHudModule = true;
    
    float m_size = 40.0f;
    bool showBackground = true;
    float m_backgroundOpacity = 0.5f;
    bool allowNegatives = false;
};
