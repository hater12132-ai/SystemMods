#pragma once

#include "../Module.hpp"

class NoFogModule : public Module {
public:
    NoFogModule();
    ~NoFogModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;

private:
    bool m_patched;
    void* m_patchTarget;

    void applyPatch();
};
