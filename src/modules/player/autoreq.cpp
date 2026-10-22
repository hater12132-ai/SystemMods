#include "autoreq.hpp"
#include "core/memory/Hooks.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include "core/GameHooks.hpp"
#include <chrono>
#include <mutex>
#include <regex>
#include <memory>

AutoReQ* AutoReQ::instance = nullptr;

static std::chrono::steady_clock::time_point sLastReQTime{};
static std::mutex sTimeMutex;
static std::string sCurrentTeamColor = "";
static std::string sCurrentGameMode = "";
static bool sListenForServer = false;

AutoReQ::AutoReQ() : Module("AutoReQ", "Automatically requeues for the next game on Hive.") {
    instance = this;
    showInMenu = true;
}

AutoReQ::~AutoReQ() {
    if (instance == this) {
        instance = nullptr;
    }
}

static void* (*sendToServer)(void* sender, void* packet) = nullptr;
static void* (*getPacketSender)(void* clientInstance) = nullptr;
static std::shared_ptr<void> (*createPacket)(int id) = nullptr;

static bool canSendReQ() {
    if (!AutoReQ::instance || !AutoReQ::instance->enabled) return false;
    std::lock_guard<std::mutex> lock(sTimeMutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - sLastReQTime).count();
    if (elapsed < AutoReQ::instance->cooldownMs) {
        return false;
    }
    sLastReQTime = now;
    return true;
}

static void sendReQCommand() {
    if (!canSendReQ()) return;
    
    
    if (sCurrentGameMode.empty()) return;
    
    if (!sendToServer) sendToServer = (decltype(sendToServer)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::LoopbackPacketSenderSendToServer);
    if (!getPacketSender) getPacketSender = (decltype(getPacketSender)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientInstanceGetPacketSender);
    if (!createPacket) createPacket = (decltype(createPacket)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MinecraftPacketsCreatePacket);
    
    if (!createPacket || !sendToServer || !getPacketSender || !bedrocktools::core::gamehooks::clientInstance()) return;

    
    std::shared_ptr<void> pktSp = createPacket(77);
    void* pkt = pktSp.get();
    if (!pkt) return;
    
    uintptr_t payload = (uintptr_t)pkt + bedrocktools::sdk::offsets::Packet::Size;
    
    
    *reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::CommandRequestPacketPayload::mCommand) = "/q " + sCurrentGameMode;
    
    
    *reinterpret_cast<uint8_t*>(payload + bedrocktools::sdk::offsets::CommandRequestPacketPayload::mOrigin + bedrocktools::sdk::offsets::CommandOriginData::mType) = 0;
    
    
    *reinterpret_cast<bool*>(payload + bedrocktools::sdk::offsets::CommandRequestPacketPayload::mInternalSource) = true;
    
    void* sender = getPacketSender(bedrocktools::core::gamehooks::clientInstance());
    if (sender) {
        sendToServer(sender, pkt);
    }
}

static void sendConnectionCommand() {
    if (!sendToServer) sendToServer = (decltype(sendToServer)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::LoopbackPacketSenderSendToServer);
    if (!getPacketSender) getPacketSender = (decltype(getPacketSender)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientInstanceGetPacketSender);
    if (!createPacket) createPacket = (decltype(createPacket)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MinecraftPacketsCreatePacket);
    
    if (!createPacket || !sendToServer || !getPacketSender || !bedrocktools::core::gamehooks::clientInstance()) return;

    std::shared_ptr<void> pktSp = createPacket(77);
    void* pkt = pktSp.get();
    if (!pkt) return;
    
    uintptr_t payload = (uintptr_t)pkt + bedrocktools::sdk::offsets::Packet::Size;
    
    *reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::CommandRequestPacketPayload::mCommand) = "/connection";
    *reinterpret_cast<uint8_t*>(payload + bedrocktools::sdk::offsets::CommandRequestPacketPayload::mOrigin + bedrocktools::sdk::offsets::CommandOriginData::mType) = 0;
    *reinterpret_cast<bool*>(payload + bedrocktools::sdk::offsets::CommandRequestPacketPayload::mInternalSource) = true;
    
    void* sender = getPacketSender(bedrocktools::core::gamehooks::clientInstance());
    if (sender) {
        sendToServer(sender, pkt);
    }
}

static bool checkTitle(std::string const& text) {
    if (text.empty() || !AutoReQ::instance) return false;
    if (AutoReQ::instance->soloMode) {
        if (text.find("\xC2\xA7" "cYou died!") != std::string::npos || 
            text.find("\xC2\xA7" "7You're spectating the") != std::string::npos) {
            return true;
        }
    }
    return false;
}

static bool checkText(std::string const& text) {
    if (text.empty() || !AutoReQ::instance) return false;
    
    if (AutoReQ::instance->gameOver) {
        if (text == "\xC2\xA7" "c\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "c\xC2\xA7" "lGame OVER!") {
            return true;
        }
    }
    
    if (AutoReQ::instance->teamElimination) {
        if (text.length() > 27) {
            if (text.substr(12, 15) == "You are on the ") {
                sCurrentTeamColor = text.substr(27, text.length() - 28);
            }
        }
        if (text.find("\xC2\xA7" "7has been \xC2\xA7" "cELIMINATED\xC2\xA7" "7!") != std::string::npos && 
            !sCurrentTeamColor.empty() && 
            text.find(sCurrentTeamColor) != std::string::npos) {
            return true;
        }
    }
    
    if (AutoReQ::instance->soloMode) {
        if (text.find("\xC2\xA7" "a\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "eYou finished all maps and came in") == 0 ||
            text.find("\xC2\xA7" "a\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "eYou finished in") == 0) {
            return true;
        }
    }
    
    if (AutoReQ::instance->roleMurderer && text == "\xC2\xA7" "c\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "c\xC2\xA7" "lMurderer") return true;
    if (AutoReQ::instance->roleSheriff && text == "\xC2\xA7" "9\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "9\xC2\xA7" "lSheriff") return true;
    if (AutoReQ::instance->roleInnocent && text == "\xC2\xA7" "a\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "a\xC2\xA7" "lInnocent") return true;
    if (AutoReQ::instance->roleDeath && text == "\xC2\xA7" "d\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "bYou are a \xC2\xA7" "cDeath") return true;
    if (AutoReQ::instance->roleRunner && text == "\xC2\xA7" "d\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "bYou are a \xC2\xA7" "aRunner") return true;
    if (AutoReQ::instance->roleHider && text == "\xC2\xA7" "e\xC2\xA7" "l\xC2\xBB \xC2\xA7" "rYou are a \xC2\xA7" "eHIDER") return true;
    if (AutoReQ::instance->roleSeeker && text == "\xC2\xA7" "c\xC2\xA7" "l\xC2\xBB \xC2\xA7" "rYou are a \xC2\xA7" "cSEEKER") return true;
    
    return false;
}

static std::string getTextPacketMessage(void* packet) {
    uintptr_t payload = (uintptr_t)packet + bedrocktools::sdk::offsets::Packet::Size;
    uint32_t variantIndex = *reinterpret_cast<uint32_t*>(payload + bedrocktools::sdk::offsets::TextPacketPayload::mVariantIndex);
    
    if (variantIndex == 1) {
        return *reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::TextPacketPayload::AuthorAndMessage::mMessage);
    } else if (variantIndex == 0 || variantIndex == 2) {
        return *reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::TextPacketPayload::MessageOnly::mMessage);
    }
    return "";
}

static void (*onHandleSetTitlePacket_orig)(void*, void*, void*) = nullptr;
static void onHandleSetTitlePacket(void* handler, void* source, void* packet) {
    if (onHandleSetTitlePacket_orig) {
        onHandleSetTitlePacket_orig(handler, source, packet);
    }

    if (AutoReQ::instance && AutoReQ::instance->enabled) {
        uintptr_t payload = (uintptr_t)packet + bedrocktools::sdk::offsets::Packet::Size;
        int packetType = *reinterpret_cast<int*>(payload + bedrocktools::sdk::offsets::SetTitlePacketPayload::mType);
        
        if (packetType == 0 || packetType == 1) { 
            std::string text = *reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::SetTitlePacketPayload::mTitleText);
            if (checkTitle(text)) {
                sendReQCommand();
            }
        }
    }
}

static void (*onHandleTextPacket_orig)(void*, void*, void*) = nullptr;
static void onHandleTextPacket(void* handler, void* source, void* packet) {
    if (AutoReQ::instance && AutoReQ::instance->enabled) {
        std::string message = getTextPacketMessage(packet);
        
        std::string textToCheck = "You are connected to server name ";
        
        if (sListenForServer && message.find(textToCheck) != std::string::npos) {
            
            size_t pos = message.find(textToCheck) + textToCheck.length();
            std::string serverLine = message.substr(pos);
            
            
            
            size_t newlinePos = serverLine.find_first_of("\r\n");
            if (newlinePos != std::string::npos) {
                serverLine = serverLine.substr(0, newlinePos);
            }
            
            
            std::regex pattern("\\d+");
            sCurrentGameMode = std::regex_replace(serverLine, pattern, "");
            sListenForServer = false;
            
            
            return; 
        } else if (sListenForServer && (message.find("You are connected") != std::string::npos || message.find(" connected to ") != std::string::npos)) {
            return; 
        }

        if (message.find("Could not connect") != std::string::npos || message.find("server is full") != std::string::npos) {
            sLastReQTime = std::chrono::steady_clock::time_point{};
            sendReQCommand();
            return;
        }

        if (checkText(message)) {
            sendReQCommand();
        }
    }
    
    if (onHandleTextPacket_orig) {
        onHandleTextPacket_orig(handler, source, packet);
    }
}

static void (*onChangeDimension_orig)(void*, void*) = nullptr;
static void onChangeDimension(void* player, void* packet) {
    if (AutoReQ::instance && AutoReQ::instance->enabled) {
        uintptr_t payload = (uintptr_t)packet + bedrocktools::sdk::offsets::Packet::Size;
        
        int dimId = *reinterpret_cast<int*>(payload + bedrocktools::sdk::offsets::ChangeDimensionPacketPayload::mDimensionId);
        
        
        if (dimId == 0) {
            sListenForServer = true;
            sendConnectionCommand();
        }
    }
    
    if (onChangeDimension_orig) {
        onChangeDimension_orig(player, packet);
    }
}

void AutoReQ::onInit() {
    uintptr_t setTitleAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientNetworkHandlerHandleSetTitle);
    if (setTitleAddr) {
        bedrocktools::hooks::install((void*)setTitleAddr, (void*)onHandleSetTitlePacket, (void**)&onHandleSetTitlePacket_orig);
    }
    
    uintptr_t setTxtAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientNetworkHandlerHandleText);
    if (setTxtAddr) {
        bedrocktools::hooks::install((void*)setTxtAddr, (void*)onHandleTextPacket, (void**)&onHandleTextPacket_orig);
    }
    
    uintptr_t changeDimAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::LocalPlayerChangeDimension);
    if (changeDimAddr) {
        bedrocktools::hooks::install((void*)changeDimAddr, (void*)onChangeDimension, (void**)&onChangeDimension_orig);
    }
}

void AutoReQ::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("soloMode")) soloMode = j["soloMode"].get<bool>();
    if (j.contains("teamElimination")) teamElimination = j["teamElimination"].get<bool>();
    if (j.contains("gameOver")) gameOver = j["gameOver"].get<bool>();
    if (j.contains("roleMurderer")) roleMurderer = j["roleMurderer"].get<bool>();
    if (j.contains("roleSheriff")) roleSheriff = j["roleSheriff"].get<bool>();
    if (j.contains("roleInnocent")) roleInnocent = j["roleInnocent"].get<bool>();
    if (j.contains("roleHider")) roleHider = j["roleHider"].get<bool>();
    if (j.contains("roleSeeker")) roleSeeker = j["roleSeeker"].get<bool>();
    if (j.contains("roleDeath")) roleDeath = j["roleDeath"].get<bool>();
    if (j.contains("roleRunner")) roleRunner = j["roleRunner"].get<bool>();
}

void AutoReQ::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["soloMode"] = soloMode;
    j["teamElimination"] = teamElimination;
    j["gameOver"] = gameOver;
    j["roleMurderer"] = roleMurderer;
    j["roleSheriff"] = roleSheriff;
    j["roleInnocent"] = roleInnocent;
    j["roleHider"] = roleHider;
    j["roleSeeker"] = roleSeeker;
    j["roleDeath"] = roleDeath;
    j["roleRunner"] = roleRunner;
}
