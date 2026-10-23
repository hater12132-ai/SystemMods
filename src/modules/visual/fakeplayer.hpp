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

    void spawnAtPlayer();

    /** 0 = same coords as you (copy). >0 = blocks in front. */
    float distance = 0.0f;
    float heightOffset = 0.0f;
    bool faceYou = false;
    bool drawBody = true;
    /** If true, dummy stays glued to your position every tick (moving copy). */
    bool followPlayer = false;
    float bodyScale = 1.0f;
    std::string dummyName = "FakePlayer";
    bool wantSpawn = false;
};
