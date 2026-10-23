#pragma once

#include "../Module.hpp"

class FakePlayerModule : public Module {
public:
    FakePlayerModule();
    ~FakePlayerModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    void spawnInFront();

    float distance = 3.0f;
    float heightOffset = 0.0f;
    bool faceYou = true;
    bool drawBody = true;
    bool drawName = true;
    bool lockPosition = true;
    float bodyScale = 1.0f;
    std::string dummyName = "FakePlayer";
    bool m_wantSpawn = false;
};
