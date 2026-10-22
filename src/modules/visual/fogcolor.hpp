#pragma once
#include "../Module.hpp"
#include <cstdint>

class FogColorModule : public Module {
private:
    uint32_t m_colorHex = 0xFFFF0000;
    bool    m_rainbow = false;
    float   m_rainbowSpeed = 1.0f;
    float   m_rainbowHue = 0.0f;

    bool    m_patched;
    uint8_t m_originalBytes[8];
    void*   m_patchTarget;

    void applyPatch();

public:
    void applyColorsToPointers(float* r_ptr, float* g_ptr, float* b_ptr);
    FogColorModule();
    ~FogColorModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;
};
