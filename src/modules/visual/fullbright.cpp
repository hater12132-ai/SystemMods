#include "fullbright.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <cstring>

static constexpr size_t FULLBRIGHT_PATCH_SIZE = 12;

FullbrightModule::FullbrightModule()
    : Module("Fullbright", "Removes darkness by setting the game's light level to maximum everywhere") {}

void FullbrightModule::onInit() {
    if (m_patchTarget) return;
    uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::Fullbright);
    if (addr == 0) return;
    m_patchTarget = reinterpret_cast<void*>(addr);
}

void FullbrightModule::onEnable() {
    if (m_patched || !m_patchTarget) return;
    
    uint8_t patch[FULLBRIGHT_PATCH_SIZE] = {
        0x40, 0x8F, 0xA8, 0x52,
        0x00, 0x00, 0x27, 0x1E,
        0xC0, 0x03, 0x5F, 0xD6
    };

    memcpy(m_originalBytes, m_patchTarget, FULLBRIGHT_PATCH_SIZE);
    if (bedrocktools::sdk::patchMemory(m_patchTarget, patch, FULLBRIGHT_PATCH_SIZE))
        m_patched = true;
}

void FullbrightModule::onDisable() {
    if (!m_patched || !m_patchTarget) return;
    if (bedrocktools::sdk::patchMemory(m_patchTarget, m_originalBytes, FULLBRIGHT_PATCH_SIZE))
        m_patched = false;
}

