#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>
#include <vector>
#include <string>

class BreadcrumbsModule : public Module {
public:
    BreadcrumbsModule();
    ~BreadcrumbsModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    uint32_t trailColor = 0xFF00FF00;
    int tickInterval = 5;
    int maxPoints = 1000;

    std::vector<bedrocktools::sdk::Vec3> points;
    int tickCounter = 0;

    void clearTrail();

private:
    bool m_patched;
    void* m_patchTarget;

    void* m_tessBeginAddr;
    void* m_tessColorAddr;
    void* m_tessVertexAddr;
    void* m_renderMaterialGroupAddr;
    void* m_renderMeshAddr;
    void* m_renderMesh2Addr;

    void applyPatch();
};
