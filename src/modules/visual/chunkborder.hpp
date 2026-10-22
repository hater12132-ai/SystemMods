#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>
#include <string>

class ChunkBorderModule : public Module {
public:
    ChunkBorderModule();
    ~ChunkBorderModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    float vertLineSpacing;
    int horizLineSpacing;
    uint32_t cornerColor;
    uint32_t midColor;
    uint32_t adjColor;

private:
    bool m_patched;
    void* m_patchTarget;

    void* m_tessBeginAddr;
    void* m_tessColorAddr;
    void* m_tessVertexAddr;
    void* m_renderMaterialGroupAddr;

    void applyPatch();
};
