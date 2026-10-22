#include "nick.hpp"
#include "core/memory/Hooks.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <map>
#include <string>

static NickModule* g_nickMod = nullptr;

struct MCColor { std::string code; int r, g, b; };
static std::vector<MCColor> mcColors = {
    {"\xC2\xA7" "0", 0, 0, 0},
    {"\xC2\xA7" "1", 0, 0, 170},
    {"\xC2\xA7" "2", 0, 170, 0},
    {"\xC2\xA7" "3", 0, 170, 170},
    {"\xC2\xA7" "4", 170, 0, 0},
    {"\xC2\xA7" "5", 170, 0, 170},
    {"\xC2\xA7" "6", 255, 170, 0},
    {"\xC2\xA7" "7", 170, 170, 170},
    {"\xC2\xA7" "8", 85, 85, 85},
    {"\xC2\xA7" "9", 85, 85, 255},
    {"\xC2\xA7" "a", 85, 255, 85},
    {"\xC2\xA7" "b", 85, 255, 255},
    {"\xC2\xA7" "c", 255, 85, 85},
    {"\xC2\xA7" "d", 255, 85, 255},
    {"\xC2\xA7" "e", 255, 255, 85},
    {"\xC2\xA7" "f", 255, 255, 255},
    {"\xC2\xA7" "g", 221, 214, 5}
};

static std::string getClosestMCColor(uint32_t hex) {
    int r = (hex >> 16) & 0xFF;
    int g = (hex >> 8) & 0xFF;
    int b = hex & 0xFF;
    int bestDist = 9999999;
    std::string bestCode = "\xC2\xA7" "f";
    for(auto& c : mcColors) {
        int dr = r - c.r;
        int dg = g - c.g;
        int db = b - c.b;
        int dist = dr*dr + dg*dg + db*db;
        if(dist < bestDist) {
            bestDist = dist;
            bestCode = c.code;
        }
    }
    return bestCode;
}

static void (*_drawText_orig)(void*, void*, void*, std::string*, void*, float, int, void*, void*) = nullptr;
static void _drawText_hook(void* _this, void* font, void* rect, std::string* text, void* color, float alpha, int alignment, void* textData, void* caretData) {
    if (g_nickMod && g_nickMod->enabled && text && !g_nickMod->m_originalName.empty()) {
        std::string localName = g_nickMod->m_originalName;
        size_t pos = text->find(localName);
        if (pos != std::string::npos) {
            std::string prefix = getClosestMCColor(g_nickMod->m_textColor);
            if (g_nickMod->m_italic) prefix += "\xC2\xA7" "o";
            if (g_nickMod->m_bold) prefix += "\xC2\xA7" "l";
            if (g_nickMod->m_obfuscated) prefix += "\xC2\xA7" "k";
            
            std::string faketxt = *text;
            std::string nickName = g_nickMod->m_fakeName;
            size_t colorCodePos = nickName.find("\xC2\xA7");
            while (colorCodePos != std::string::npos && colorCodePos + 1 < nickName.length()) {
                nickName.erase(colorCodePos, 2);
                colorCodePos = nickName.find("\xC2\xA7");
            }
            faketxt.replace(pos, localName.length(), prefix + nickName + "\xC2\xA7r");
            if (_drawText_orig) {
                _drawText_orig(_this, font, rect, &faketxt, color, alpha, alignment, textData, caretData);
            }
            return;
        }
    }
    if (_drawText_orig) {
        _drawText_orig(_this, font, rect, text, color, alpha, alignment, textData, caretData);
    }
}

static void (*_setNameTag_orig)(void*, std::string*) = nullptr;
static void nickTick(void* _this) {
    if (g_nickMod && g_nickMod->enabled) {
        std::string* pName = (std::string*)((uintptr_t)_this + bedrocktools::sdk::offsets::Player::mName);
        std::string* pFilteredNameTag = (std::string*)((uintptr_t)_this + bedrocktools::sdk::offsets::Actor::mFilteredNameTag);
        
        if (g_nickMod->m_originalName.empty()) {
            g_nickMod->m_originalName = *pName;
        }
        if (g_nickMod->m_originalNametag.empty() || g_nickMod->m_backupNametag.empty()) {
            g_nickMod->m_originalNametag = *pFilteredNameTag;
            g_nickMod->m_backupNametag = g_nickMod->m_originalNametag;
        }
        
        std::string prefix = getClosestMCColor(g_nickMod->m_textColor);
        if (g_nickMod->m_italic) prefix += "\xC2\xA7" "o";
        if (g_nickMod->m_bold) prefix += "\xC2\xA7" "l";
        if (g_nickMod->m_obfuscated) prefix += "\xC2\xA7" "k";
        
        std::string nickName = g_nickMod->m_fakeName;
        size_t colorCodePos = nickName.find("\xC2\xA7");
        while (colorCodePos != std::string::npos && colorCodePos + 1 < nickName.length()) {
            nickName.erase(colorCodePos, 2);
            colorCodePos = nickName.find("\xC2\xA7");
        }
        
        std::string val = prefix + nickName + "\xC2\xA7r";
        
        if (*pFilteredNameTag != val) {
            if (_setNameTag_orig) {
                _setNameTag_orig(_this, &val);
            }
        }
        if (*pName != val) {
            *pName = val;
        }
    } else if (g_nickMod && !g_nickMod->enabled) {
        std::string* pName = (std::string*)((uintptr_t)_this + bedrocktools::sdk::offsets::Player::mName);
        std::string* pFilteredNameTag = (std::string*)((uintptr_t)_this + bedrocktools::sdk::offsets::Actor::mFilteredNameTag);
        
        if (!g_nickMod->m_originalNametag.empty() && !g_nickMod->m_backupNametag.empty()) {
            if (g_nickMod->m_originalNametag == g_nickMod->m_fakeName) {
                g_nickMod->m_originalNametag = g_nickMod->m_backupNametag;
            }
            if (*pFilteredNameTag != g_nickMod->m_originalNametag) {
                if (_setNameTag_orig) {
                    _setNameTag_orig(_this, &g_nickMod->m_originalNametag);
                }
            }
        }
        if (!g_nickMod->m_originalName.empty()) {
            if (*pName != g_nickMod->m_originalName) {
                *pName = g_nickMod->m_originalName;
            }
        }
    }
}

NickModule::NickModule() 
    : Module("Nick", "Change your name locally.") {
    g_nickMod = this;
}

NickModule::~NickModule() {
    if (g_nickMod == this) g_nickMod = nullptr;
}

void NickModule::onInit() {
    if (!m_drawTextHooked) {
        uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MinecraftUIRenderContextDrawText);
        if (addr != 0) {
            bedrocktools::hooks::install((void*)addr, (void*)_drawText_hook, (void**)&_drawText_orig);
            m_drawTextHooked = true;
        }
    }
    
    if (!m_tickHooked) {
        bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { nickTick(event.player); });
        m_tickHooked = true;
        uintptr_t setNameTagAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorSetNameTag);
        if (setNameTagAddr != 0) {
            _setNameTag_orig = (void (*)(void*, std::string*))setNameTagAddr;
        }
    }
}

void NickModule::onEnable() {}

void NickModule::onDisable() {}

void NickModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("m_fakeName")) m_fakeName = j["m_fakeName"].get<std::string>();
    if (j.contains("m_bold")) m_bold = j["m_bold"].get<bool>();
    if (j.contains("m_italic")) m_italic = j["m_italic"].get<bool>();
    if (j.contains("m_obfuscated")) m_obfuscated = j["m_obfuscated"].get<bool>();
    
    if (j.contains("m_textColor")) {
        std::string hexStr = j["m_textColor"].get<std::string>();
        if (hexStr.length() > 0 && hexStr[0] == '#') {
            try {
                m_textColor = std::stoul(hexStr.substr(1), nullptr, 16);
            } catch (...) {}
        }
    }
}

void NickModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["m_fakeName"] = m_fakeName;
    j["m_bold"] = m_bold;
    j["m_italic"] = m_italic;
    j["m_obfuscated"] = m_obfuscated;
    
    char hexStr[10];
    snprintf(hexStr, sizeof(hexStr), "#%08X", m_textColor);
    j["m_textColor"] = std::string(hexStr);
}
