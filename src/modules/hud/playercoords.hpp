#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>

class PlayerCoordsModule : public Module {
public:
    PlayerCoordsModule();
    ~PlayerCoordsModule() override;
    
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;
    
    void updateCoords(const bedrocktools::sdk::Vec3& pos);

private:
    bedrocktools::sdk::Vec3  m_currentPos;
    float hudPosX = 20.0f;
    float hudPosY = 60.0f;
    bool isHudModule = true;
    
    float m_size = 40.0f;
    bool m_background = false;
    float m_backgroundOpacity = 0.5f;
};
