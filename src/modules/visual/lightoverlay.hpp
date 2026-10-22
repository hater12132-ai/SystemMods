#pragma once

#include "../Module.hpp"
#include <bedrocktools/sdk/Types.hpp>
#include <vector>

class LightOverlayModule : public Module {
public:
    LightOverlayModule();
    ~LightOverlayModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    
    int radiusHorizontal = 16;
    int radiusVertical = 8;
    bool onlyTopFace = false;
    bool onlySolidBlocks = true;
    
    
    uint32_t safeColor = 0xFF00FF00;    
    uint32_t dangerColor = 0xFFFF0000;  
    int dangerThreshold = 7; 

private:
    void applyPatch();

    bool m_patched;
    void* m_patchTarget;

    void* m_tessBeginAddr;
    void* m_tessColorAddr;
    void* m_tessVertexAddr;
    void* m_renderMeshAddr;
    void* m_renderMesh2Addr;
    void* m_renderMaterialGroupAddr;
};
