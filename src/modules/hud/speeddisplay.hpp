#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>

class SpeedDisplayModule : public Module {
public:
    SpeedDisplayModule();
    ~SpeedDisplayModule() override;
    
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;
    
    void updatePosition(const bedrocktools::sdk::Vec3& pos);

private:
    float m_currentSpeed = 0.0f;
    bool  m_use3D = false;
    
    bedrocktools::sdk::Vec3  m_lastPos;
    bool  m_firstTick = true;
    
    float hudPosX = 20.0f;
    float hudPosY = 100.0f;
    bool isHudModule = true;
    
    float m_size = 40.0f;
    bool m_background = false;
    float m_backgroundOpacity = 0.5f;
};
