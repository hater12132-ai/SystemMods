#include "nodisconnect.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include "core/memory/Hooks.hpp"

static bool (*original_isInEDUMultiplayerSession)(void* _this) = nullptr;
static NoDisconnectModule* g_noDisconnectMod = nullptr;

static bool isInEDUMultiplayerSession_hook(void* _this) {
    if (g_noDisconnectMod && g_noDisconnectMod->enabled) {
        return true;
    }
    
    if (original_isInEDUMultiplayerSession) {
        return original_isInEDUMultiplayerSession(_this);
    }
    return false;
}

NoDisconnectModule::NoDisconnectModule()
    : Module("No Disconnect", "Prevents you from being disconnected when you minimize the app.") {
    showInMenu = false; // always-on client feature, not a user module
    g_noDisconnectMod = this;
}

void NoDisconnectModule::onInit() {
    if (m_patchTarget) return;
    
    uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::EduMultiplayer);
    if (addr != 0) {
        m_patchTarget = (void*)addr;
        bedrocktools::hooks::install(m_patchTarget, (void*)isInEDUMultiplayerSession_hook, (void**)&original_isInEDUMultiplayerSession);
        m_patched = true;
    }
    // Always on — part of the client, not a toggle
    setMasterEnabled(true);
}

void NoDisconnectModule::onEnable() {
}

void NoDisconnectModule::onDisable() {
}

