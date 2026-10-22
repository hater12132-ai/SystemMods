#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>
#include <chrono>

class ReachCounterModule : public Module {
public:
    ReachCounterModule();
    ~ReachCounterModule() override;
    
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;
    
    void updateReach(float reach);

private:
    float m_currentReach = 0.0f;
    std::chrono::high_resolution_clock::time_point m_lastHitTime;

    float hudPosX = 20.0f;
    float hudPosY = 100.0f;
    bool isHudModule = true;
    
    float m_size = 40.0f;
    bool m_background = false;
    float m_backgroundOpacity = 0.5f;
};
