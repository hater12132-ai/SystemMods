#include "autogg.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include "core/memory/Hooks.hpp"
#include "core/GameHooks.hpp"
#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <memory>
#include <regex>

AutoGG* AutoGG::instance = nullptr;

static std::chrono::steady_clock::time_point sLastGGTime{};
static std::mutex sTimeMutex;

AutoGG::AutoGG() : Module("AutoGG", "Automatically says GG at the end of a game.") {
    instance = this;
    showInMenu = true;
}

static void* (*sendToServer)(void* sender, void* packet) = nullptr;
static void* (*getPacketSender)(void* clientInstance) = nullptr;
static std::shared_ptr<void> (*createPacket)(int id) = nullptr;

static bool canSendGG() {
    if (!AutoGG::instance || !AutoGG::instance->enabled) return false;
    std::lock_guard<std::mutex> lock(sTimeMutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - sLastGGTime).count();
    if (elapsed < 3000) { 
        return false;
    }
    sLastGGTime = now;
    return true;
}

static void sendGGCommand() {
    if (!canSendGG()) return;
    
    
    if (!sendToServer) sendToServer = (decltype(sendToServer)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::LoopbackPacketSenderSendToServer);
    if (!getPacketSender) getPacketSender = (decltype(getPacketSender)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientInstanceGetPacketSender);
    if (!createPacket) createPacket = (decltype(createPacket)) bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MinecraftPacketsCreatePacket);
    
    if (!createPacket || !sendToServer || !getPacketSender || !bedrocktools::core::gamehooks::clientInstance()) return;

    std::shared_ptr<void> pktSp = createPacket(9); 
    
    void* pkt = pktSp.get();
    if (!pkt) return;
    
    uintptr_t payload = (uintptr_t)pkt + bedrocktools::sdk::offsets::Packet::Size;
    
    std::string* msgOnlyStr = reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::TextPacketPayload::MessageOnly::mMessage);
    msgOnlyStr->~basic_string();

    *reinterpret_cast<uint32_t*>(payload + bedrocktools::sdk::offsets::TextPacketPayload::mVariantIndex) = 1;

    *reinterpret_cast<uint8_t*>(payload + bedrocktools::sdk::offsets::TextPacketPayload::AuthorAndMessage::mType) = 1;
    new ((void*)(payload + bedrocktools::sdk::offsets::TextPacketPayload::AuthorAndMessage::mAuthor)) std::string("");
    std::string customGg = AutoGG::instance ? AutoGG::instance->ggMessage : "gg";
    new ((void*)(payload + bedrocktools::sdk::offsets::TextPacketPayload::AuthorAndMessage::mMessage)) std::string(customGg);
    
    
    void* sender = getPacketSender(bedrocktools::core::gamehooks::clientInstance());
    if (sender) {
        sendToServer(sender, pkt);
    }
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

static bool checkTitle(std::string const& text) {
    if (text.empty() || !AutoGG::instance) return false;
    
    if (text.find("\xC2\xA7" "f\xC2\xA7" "aYou won the game!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "f\xC2\xA7" "cYou lost the game!") != std::string::npos) return true;
    if (text == "   ") return true;
    if (text.find("Team\xC2\xA7" "r\xC2\xA7" "a won the game!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "bHiders\xC2\xA7" "r\xC2\xA7" "f Win") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "eSeekers\xC2\xA7" "r\xC2\xA7" "f Win") != std::string::npos) return true;
    if (text.find("Finished") != std::string::npos) return true;
    if (text.find("Out of Time!") != std::string::npos) return true;

    static std::regex const galaxiteRegex(
        R"(Is The \xC2\xA76\xC2\xA7l(Chronos|Rush) (Champion|Champions)!)",
        std::regex::optimize
    );
    if (std::regex_search(text, galaxiteRegex)) return true;

    if (text.find("\xC2\xA7" "aYou Win!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "cGame Over!") != std::string::npos) return true;
    
    
    if (text == "\xC2\xA7" "cYou died!" || text == "\xC2\xA7" "7You're spectating the \xC2\xA7" "as\xC2\xA7" "eh\xC2\xA7" "6o\xC2\xA7" "cw\xC2\xA7" "7!") return true;
    
    return false;
}

static bool checkText(std::string const& text) {
    if (text.empty() || !AutoGG::instance) return false;

    if (text.find("Finished") != std::string::npos) return true;
    if (text.find("Out of Time!") != std::string::npos) return true;
    if (text.find("You won the game!") != std::string::npos) return true;
    if (text.find("You lost the game!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "aYou Win!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "cGame Over!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "c\xC2\xA7" "l\xC2\xBB \xC2\xA7" "r\xC2\xA7" "c\xC2\xA7" "lGame OVER!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "r\xC2\xA7" "c\xC2\xA7" "lGame OVER!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "a won the game!") != std::string::npos) return true;
    if (text.find("\xC2\xA7" "a has won the game!") != std::string::npos) return true;

    if (text == "Game OVER!" || text == "You are out of lives!") return true;
    
    return false;
}

static void (*onHandleSetTitlePacket_orig)(void*, void*, void*) = nullptr;
static void onHandleSetTitlePacket(void* handler, void* source, void* packet) {
    if (onHandleSetTitlePacket_orig) {
        onHandleSetTitlePacket_orig(handler, source, packet);
    }

    if (AutoGG::instance && AutoGG::instance->enabled) {
        uintptr_t payload = (uintptr_t)packet + bedrocktools::sdk::offsets::Packet::Size;
        int packetType = *reinterpret_cast<int*>(payload + bedrocktools::sdk::offsets::SetTitlePacketPayload::mType);
        
        if (packetType == 0 || packetType == 1) {
            std::string text = *reinterpret_cast<std::string*>(payload + bedrocktools::sdk::offsets::SetTitlePacketPayload::mTitleText);
            if (checkTitle(text)) {
                sendGGCommand();
            }
        }
    }
}

static void (*onHandleTextPacket_orig)(void*, void*, void*) = nullptr;
static void onHandleTextPacket(void* handler, void* source, void* packet) {
    if (onHandleTextPacket_orig) {
        onHandleTextPacket_orig(handler, source, packet);
    }

    if (AutoGG::instance && AutoGG::instance->enabled) {
        std::string message = getTextPacketMessage(packet);
        if (checkText(message)) {
            sendGGCommand();
        }
    }
}

void AutoGG::onInit() {
    uintptr_t setTitleAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientNetworkHandlerHandleSetTitle);
    if (setTitleAddr) {
        bedrocktools::hooks::install((void*)setTitleAddr, (void*)onHandleSetTitlePacket, (void**)&onHandleSetTitlePacket_orig);
    }
    
    uintptr_t setTxtAddr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ClientNetworkHandlerHandleText);
    if (setTxtAddr) {
        bedrocktools::hooks::install((void*)setTxtAddr, (void*)onHandleTextPacket, (void**)&onHandleTextPacket_orig);
    }
}

void AutoGG::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("ggMessage")) ggMessage = j["ggMessage"].get<std::string>();
}

void AutoGG::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["ggMessage"] = ggMessage;
}
