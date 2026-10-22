#include "fogcolor.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include "core/memory/Hooks.hpp"
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <cmath>

static void customHSVtoRGB(float h, float s, float v, float& out_r, float& out_g, float& out_b) {
    if (s == 0.0f) {
        out_r = out_g = out_b = v;
        return;
    }
    h = std::fmod(h, 1.0f) * 6.0f;
    int i = (int)std::floor(h);
    float f = h - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
        case 0: out_r = v; out_g = t; out_b = p; break;
        case 1: out_r = q; out_g = v; out_b = p; break;
        case 2: out_r = p; out_g = v; out_b = t; break;
        case 3: out_r = p; out_g = q; out_b = v; break;
        case 4: out_r = t; out_g = p; out_b = v; break;
        default: out_r = v; out_g = p; out_b = q; break;
    }
}

static void (*_setupFogPlayer_orig)(void* _this, void* a2);
static FogColorModule* g_fogMod = nullptr; 

static void _setupFogPlayer_hook(void* _this, void* a2) {
    if (_setupFogPlayer_orig) {
        _setupFogPlayer_orig(_this, a2);
    }

    if (g_fogMod && g_fogMod->enabled) {
        float* red   = (float*)((uintptr_t)_this + bedrocktools::sdk::offsets::LevelRendererPlayer::mFogColorRed);
        float* green = (float*)((uintptr_t)_this + bedrocktools::sdk::offsets::LevelRendererPlayer::mFogColorGreen);
        float* blue  = (float*)((uintptr_t)_this + bedrocktools::sdk::offsets::LevelRendererPlayer::mFogColorBlue);

        g_fogMod->applyColorsToPointers(red, green, blue);
    }
}

void FogColorModule::applyColorsToPointers(float* r_ptr, float* g_ptr, float* b_ptr) {
    float r = ((m_colorHex >> 16) & 0xFF) / 255.0f;
    float g = ((m_colorHex >> 8) & 0xFF) / 255.0f;
    float b = (m_colorHex & 0xFF) / 255.0f;

    if (m_rainbow) {
        m_rainbowHue += 0.002f * m_rainbowSpeed;
        if (m_rainbowHue > 1.0f) m_rainbowHue -= 1.0f;
        customHSVtoRGB(m_rainbowHue, 1.0f, 1.0f, r, g, b);
    }

    *r_ptr = r;
    *g_ptr = g;
    *b_ptr = b;
}


FogColorModule::FogColorModule() : Module("Fog Color", "Changes the world fog/clear color to a custom color.") {
    m_colorHex = 0xFFFF0000;
    m_patched = false;
    m_patchTarget = nullptr;
    g_fogMod = this;
}

FogColorModule::~FogColorModule() {
    if (g_fogMod == this) g_fogMod = nullptr;
}

void FogColorModule::onInit() {
    if (m_patchTarget) return;
    uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::SetupFogPlayer);
    if (addr != 0) {
        m_patchTarget = (void*)addr;
    }
}

void FogColorModule::applyPatch() {
    if (m_patched || !m_patchTarget) return;
    bedrocktools::hooks::install(m_patchTarget, (void*)_setupFogPlayer_hook, (void**)&_setupFogPlayer_orig);
    m_patched = true;
}

void FogColorModule::onEnable() {
    applyPatch(); 
}

void FogColorModule::onDisable() {
}


void FogColorModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("color")) {
        std::string hexStr = j["color"].get<std::string>();
        if (hexStr.length() > 0 && hexStr[0] == '#') {
            try {
                m_colorHex = std::stoul(hexStr.substr(1), nullptr, 16);
            } catch (...) {}
        }
    }
    if (j.contains("rainbow")) {
        m_rainbow = j["rainbow"].get<bool>();
    }
    if (j.contains("rainbowSpeed")) {
        m_rainbowSpeed = j["rainbowSpeed"].get<float>();
    }
}

void FogColorModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    char hexStr[10];
    snprintf(hexStr, sizeof(hexStr), "#%08X", m_colorHex);
    j["color"] = std::string(hexStr);
    j["rainbow"] = m_rainbow;
    j["rainbowSpeed"] = m_rainbowSpeed;
}
