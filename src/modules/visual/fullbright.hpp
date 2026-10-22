#pragma once

#include "../Module.hpp"
#include <cstdint>
#include <cstring>

class FullbrightModule : public Module {
public:
    FullbrightModule();
    void onInit() override;
    void onEnable() override;
    void onDisable() override;

private:
    bool    m_patched = false;
    uint8_t m_originalBytes[12] = {};
    void*   m_patchTarget = nullptr;
};
