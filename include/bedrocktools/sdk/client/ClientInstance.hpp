#pragma once

#include <bedrocktools/Api.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Functions.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/render/LevelRenderer.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>
#include <bedrocktools/sdk/world/BlockSource.hpp>

namespace bedrocktools::sdk {

class ClientInstance {
public:
    static ClientInstance* current() {
        const auto* runtime = api::find();
        return api::compatible(runtime) && runtime->clientInstance ? runtime->clientInstance() : nullptr;
    }

    BlockSource* region() { return virtualCall<BlockSource*>(this, offsets::VTable::ClientInstance_getRegion); }
    void* minecraftGame() { return virtualCall<void*>(this, offsets::VTable::ClientInstanceGetMinecraftGame); }
    LevelRenderer* levelRenderer() { return field<LevelRenderer*>(this, offsets::ClientInstance::mLevelRenderer); }

    Player* localPlayer(const api::ApiV1* runtime = nullptr) {
        using Function = Player*(*)(ClientInstance*);
        auto target = function<Function>(memory::SignatureId::ClientInstanceGetLocalPlayer, runtime);
        return target ? target(this) : nullptr;
    }

    void* packetSender(const api::ApiV1* runtime = nullptr) {
        using Function = void*(*)(ClientInstance*);
        auto target = function<Function>(memory::SignatureId::ClientInstanceGetPacketSender, runtime);
        return target ? target(this) : nullptr;
    }
};

}
