#pragma once

#include "../Module.hpp"

class NoDisconnectModule : public Module {
public:
    NoDisconnectModule();
    void onInit() override;
    void onEnable() override;
    void onDisable() override;

private:
    bool m_patched = false;
    void* m_patchTarget = nullptr;
};
