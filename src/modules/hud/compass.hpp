#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>

class CompassModule : public Module {
public:
    CompassModule();
    ~CompassModule() override;

    void onInit()     override;
    void onEnable()   override;
    void onDisable()  override;
    void onFrame()    override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j)       override;
    
    float m_yaw = 0.f;
    float m_pitch = 0.f;
    float hudPosX = 400.f;
    float hudPosY = 50.f;
    bool isHudModule = true;

    float m_barWidth = 400.f;
    float m_barHeight = 40.f;
    float m_scale = 1.0f;
    float m_range = 90.0f;
    float m_opacity = 0.8f;
    float m_colorR = 1.0f;
    float m_colorG = 1.0f;
    float m_colorB = 1.0f;

    float m_animYaw = 0.f;
};
