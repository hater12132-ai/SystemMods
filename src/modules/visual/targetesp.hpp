#pragma once

#include "../Module.hpp"
#include <cstdint>

class TargetESPModule : public Module {
public:
    TargetESPModule();
    ~TargetESPModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    bool showSelf = false;
    float maxDistance = 48.0f;
    float ringHeight = 0.15f;      // vertical offset of main ring from AABB bottom
    float animSpeed = 1.6f;
    int chainLinks = 8;
    uint32_t ringColor = 0xFFFFFFFF;   // ARGB
    uint32_t chainColor = 0xFFFFAA33;  // warm orange like the video
    uint32_t accentColor = 0x88FFFFFF;

private:
    bool m_patched = false;
    void* m_patchTarget = nullptr;
    void applyPatch();
};
