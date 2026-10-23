#pragma once

#include "../Module.hpp"
#include <cstdint>

class TargetESPModule : public Module {
public:
    enum Style : int {
        StyleChains = 0,
        StyleNova = 1,
        StyleWild = 2,
        StyleRhombus = 3,
        StyleScanner = 4,
        StyleVega = 5,
        StyleSwords = 6,
        StyleCrystals = 7,
        StyleJello = 8,
        StyleGhost = 9,
        StyleBubble = 10,
        StyleHelix = 11,
        StyleOrbit = 12,
        StyleCount = 13
    };

    TargetESPModule();
    ~TargetESPModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    bool showSelf = false;
    bool onlyLookingAt = false;   // if true, only ESP the crosshair player
    float maxDistance = 48.0f;
    float ringHeight = 0.08f;
    float animSpeed = 1.8f;
    float thickness = 1.0f;       // multi-pass scale
    int chainLinks = 10;
    int style = StyleChains;
    uint32_t ringColor = 0xFFFFFFFF;
    uint32_t chainColor = 0xFFFFAA33;
    uint32_t accentColor = 0xAAFF66FF;

private:
    bool m_patched = false;
    void* m_patchTarget = nullptr;
    void applyPatch();
};
