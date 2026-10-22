#include "skinstealer.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/events/EventBus.hpp>
#include "../../config/ConfigManager.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <vector>
#include <string>
#include <cctype>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>


using LevelGetHitResultFn = void*(*)(void*);
static LevelGetHitResultFn getHitResultFn = nullptr;

using HitResultGetEntityFn = void*(*)(void*);
static HitResultGetEntityFn getEntityFn = nullptr;

using ActorIsPlayerFn = bool(*)(void*);
static ActorIsPlayerFn isPlayerFn = nullptr;

static SkinStealerModule* g_skinStealer = nullptr;
static void* g_localPlayerNative = nullptr;

static void onTickHook(void* _this) {
    if (g_skinStealer && g_skinStealer->enabled) {
        g_localPlayerNative = _this;
    }
}

static std::string stripColors(const std::string& input) {
    std::string output;
    for (size_t i = 0; i < input.length(); ++i) {
        if ((unsigned char)input[i] == 0xC2 && i + 1 < input.length() && (unsigned char)input[i+1] == 0xA7) {
            i += 2; 
        } else if ((unsigned char)input[i] == 0xA7 && i + 1 < input.length()) {
            i += 1; 
        } else {
            char c = input[i];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ') {
                output += c;
            }
        }
    }
    return output;
}

static void dumpSkin(void* targetPlayer) {
    if (!targetPlayer) return;
    
    void* skinRefPtr = *(void**)((uintptr_t)targetPlayer + bedrocktools::sdk::offsets::Player::mSkin);
    if (!skinRefPtr) return;
    
    void* skinImplSharedPtrBase = (void*)((uintptr_t)skinRefPtr + bedrocktools::sdk::offsets::SerializedSkinRef::mSkinImpl);
    void* threadOwner = *(void**)skinImplSharedPtrBase;
    if (!threadOwner) return;
    
    void* skinImpl = (void*)((uintptr_t)threadOwner + bedrocktools::sdk::offsets::ThreadOwner::mObject);
    if (!skinImpl) return;
    
    void* targetImage = (void*)((uintptr_t)skinImpl + bedrocktools::sdk::offsets::SerializedSkinImpl::mSkinImage);
    
    bool isPersona = *(bool*)((uintptr_t)skinImpl + bedrocktools::sdk::offsets::SerializedSkinImpl::mIsPersona);
    if (isPersona) {
        uintptr_t vecBegin = *(uintptr_t*)((uintptr_t)skinImpl + bedrocktools::sdk::offsets::SerializedSkinImpl::mSkinAnimatedImages);
        uintptr_t vecEnd   = *(uintptr_t*)((uintptr_t)skinImpl + bedrocktools::sdk::offsets::SerializedSkinImpl::mSkinAnimatedImages + 8);
        
        for (uintptr_t ptr = vecBegin; ptr < vecEnd; ptr += bedrocktools::sdk::offsets::AnimatedImageData::Size) {
            uint32_t type = *(uint32_t*)(ptr + bedrocktools::sdk::offsets::AnimatedImageData::mType);
            if (type == 3 || type == 2) { 
                targetImage = (void*)(ptr + bedrocktools::sdk::offsets::AnimatedImageData::mImage);
                if (type == 3) break;
            }
        }
    }
    
    uint32_t width = *(uint32_t*)((uintptr_t)targetImage + bedrocktools::sdk::offsets::SkinImage::mWidth);
    uint32_t height = *(uint32_t*)((uintptr_t)targetImage + bedrocktools::sdk::offsets::SkinImage::mHeight);
    
    if (width == 0 || height == 0) return;
    
    std::string* pFilteredNameTag = (std::string*)((uintptr_t)targetPlayer + bedrocktools::sdk::offsets::Actor::mFilteredNameTag);
    std::string cleanName = "unknown_player";
    if (pFilteredNameTag && !pFilteredNameTag->empty()) {
        cleanName = stripColors(*pFilteredNameTag);
        if (cleanName.empty()) cleanName = "unknown_player";
    }
    
    std::string configPath = bedrocktools::config::ConfigManager::get().getConfigPath();
    size_t lastSlash = configPath.find_last_of('/');
    std::string configDir = (lastSlash != std::string::npos) ? configPath.substr(0, lastSlash) : "/sdcard/games/BedrockTools";
    
    std::string outPath = configDir + "/" + cleanName + "_skin.png";
    
    void* pixels = *(void**)((uintptr_t)targetImage + bedrocktools::sdk::offsets::Image::mBytesOffset);
    if (!pixels) return;
    
    int result = stbi_write_png(outPath.c_str(), width, height, 4, pixels, width * 4);
    if (result != 0 && g_skinStealer) {
        g_skinStealer->showStealMessage(cleanName);
    }
}

static bool onAttackHook(void* mode, void* actor, void* a3, void* a4) {
    if (g_skinStealer && g_skinStealer->enabled) {
        if (g_localPlayerNative) {
            void* level_ptr = *(void**)((uintptr_t)g_localPlayerNative + bedrocktools::sdk::offsets::Actor::mLevel);
            if (level_ptr && getHitResultFn) {
                void* hit = getHitResultFn(level_ptr);
                if (hit && getEntityFn && isPlayerFn) {
                    void* entity = getEntityFn(hit);
                    if (entity && isPlayerFn(entity)) {
                        dumpSkin(entity);
                        return false; 
                    }
                }
            }
        }
    }
    return true; 
}

SkinStealerModule::SkinStealerModule()
    : Module("Skin Stealer", "Steals the skin of the punched user.") {
    g_skinStealer = this;
}

SkinStealerModule::~SkinStealerModule() {
    if (g_skinStealer == this) g_skinStealer = nullptr;
}

void SkinStealerModule::onInit() {
    uintptr_t getHitResultAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::LevelGetHitResult);
    if (getHitResultAddr) {
        getHitResultFn = reinterpret_cast<LevelGetHitResultFn>(getHitResultAddr);
    }
    
    uintptr_t getEntityAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::HitResultGetEntity);
    if (getEntityAddr) {
        getEntityFn = reinterpret_cast<HitResultGetEntityFn>(getEntityAddr);
    }
    
    uintptr_t isPlayerAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorIsPlayer);
    if (isPlayerAddr) {
        isPlayerFn = reinterpret_cast<ActorIsPlayerFn>(isPlayerAddr);
    }
    
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { onTickHook(event.player); });
    
    bedrocktools::events::bus().subscribe<bedrocktools::events::AttackEvent>([](auto& event) {
        if (!onAttackHook(event.gameMode, event.target, event.argument2, event.argument3)) event.cancel();
    });
}

void SkinStealerModule::onEnable() {}

void SkinStealerModule::onDisable() {}

void SkinStealerModule::showStealMessage(const std::string& name) {
    m_stolenName = name;
    m_messageDisplayTime = 3.0f; 
}

void SkinStealerModule::onFrame() {
    if (!enabled) return;
    
    if (m_messageDisplayTime > 0.0f) {
        m_messageDisplayTime -= 1.0f / 60.0f; 
        
        std::string text = "Successfully stole " + m_stolenName + "'s skin!";
        
        std::vector<PLModMenu_DrawCommand> cmds;
        
        float textSize = 36.0f; 
        
        PLModMenu_DrawCommand txtCmd = {};
        txtCmd.type = PL_DRAW_TEXT;
        txtCmd.x = -10010.0f;
        txtCmd.y = 50.0f; 
        txtCmd.w = -1.0f;
        txtCmd.h = textSize;
        txtCmd.size = textSize;
        txtCmd.color = 0xFF00FF00; 
        txtCmd.text = text.c_str();
        cmds.push_back(txtCmd);

        submitDrawCommands(moduleId, cmds);
    }
}

void SkinStealerModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
}

void SkinStealerModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
}
