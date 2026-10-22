#include "timechanger.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include "core/memory/Hooks.hpp"
#include <bedrocktools/sdk/Memory.hpp>

static int (*_getTime_orig)(void* _this);
static void (*_setTime_orig)(void* _this, int time);
static TimeChangerModule* g_timeChangerMod = nullptr;

static int _getTime_hook(void* _this) {
    if (g_timeChangerMod && g_timeChangerMod->enabled) {
        return g_timeChangerMod->getCustomTime();
    }
    if (_getTime_orig) {
        return _getTime_orig(_this);
    }
    return 0;
}

static void _setTime_hook(void* _this, int time) {
    if (g_timeChangerMod) {
        g_timeChangerMod->updateRealTime(time);
    }
    
    if (_setTime_orig) {
        if (g_timeChangerMod && g_timeChangerMod->enabled) {
            _setTime_orig(_this, g_timeChangerMod->getCustomTime());
        } else {
            _setTime_orig(_this, time);
        }
    }
}

TimeChangerModule::TimeChangerModule() 
    : Module("Ambience", "Control time of day and weather.") {
    m_customTime = 18000; 
    m_patched = false;
    m_patchTargetGetTime = nullptr;
    m_patchTargetSetTime = nullptr;
    m_realTime = 0;
    g_timeChangerMod = this;
}

TimeChangerModule::~TimeChangerModule() {
    if (g_timeChangerMod == this) g_timeChangerMod = nullptr;
}

int TimeChangerModule::getCustomTime() const {
    return m_customTime;
}

int TimeChangerModule::getRealTime(void* level) {
    if (level && _getTime_orig) {
        return _getTime_orig(level);
    }
    return m_realTime;
}

void TimeChangerModule::updateRealTime(int newTime) {
    m_realTime = newTime;
}

void TimeChangerModule::onInit() {
    if (m_patchTargetGetTime) return;
    
    uintptr_t addrGetTime = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::Time);
    if (addrGetTime != 0) {
        m_patchTargetGetTime = (void*)addrGetTime;
    }

    uintptr_t addrSetTime = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::SetTime);
    if (addrSetTime != 0) {
        m_patchTargetSetTime = (void*)addrSetTime;
    }

    applyPatch();
}

void TimeChangerModule::applyPatch() {
    if (m_patched || !m_patchTargetGetTime || !m_patchTargetSetTime) return;
    bedrocktools::hooks::install(m_patchTargetGetTime, (void*)_getTime_hook, (void**)&_getTime_orig);
    bedrocktools::hooks::install(m_patchTargetSetTime, (void*)_setTime_hook, (void**)&_setTime_orig);
    m_patched = true;
}

void TimeChangerModule::onEnable() {
    if (!m_patched) {
        applyPatch();
    }
    if (auto* weather = ModuleRegistry::get().find("systemclient.Weather Changer")) {
        weather->setMasterEnabled(true);
    }
}

void TimeChangerModule::onDisable() {
    if (auto* weather = ModuleRegistry::get().find("systemclient.Weather Changer")) {
        weather->setMasterEnabled(false);
    }
}

void TimeChangerModule::onFrame() {
}


void TimeChangerModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("m_customTime")) m_customTime = j["m_customTime"].get<int>();
    // Weather mode lives under Ambience so one menu controls both
    if (auto* weather = ModuleRegistry::get().find("systemclient.Weather Changer")) {
        weather->loadConfig(j);
    }
}

void TimeChangerModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["m_customTime"] = m_customTime;
    if (auto* weather = ModuleRegistry::get().find("systemclient.Weather Changer")) {
        weather->saveConfig(j);
    }
}
