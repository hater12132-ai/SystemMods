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

    /** Place/reposition the dummy in front of the local player. */
    void spawnInFront();

    float distance = 3.0f;
    float heightOffset = 0.0f;
    bool faceYou = true;
    bool drawBody = true;
    std::string dummyName = "FakePlayer";

private:
    bool m_patched = false;
    void* m_patchTarget = nullptr;
    void applyPatch();
};
