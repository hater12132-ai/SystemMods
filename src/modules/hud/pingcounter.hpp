#pragma once

#include "../Module.hpp"

class PingCounterModule : public Module {
public:
    PingCounterModule();
    ~PingCounterModule() override;
    
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    int m_ping = 0;
private:
    void applyPatch();
    
    bool m_patched = false;
    void* m_patchTarget = nullptr;
    
    float hudPosX = 20.0f;
    float hudPosY = 140.0f;
    bool isHudModule = true;
    
    float m_size = 40.0f;
    bool m_background = false;
    float m_backgroundOpacity = 0.5f;
};
