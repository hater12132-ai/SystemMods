#include <bedrocktools/Api.hpp>
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include "GameHooks.hpp"

namespace {
std::uintptr_t resolveSignature(std::uint16_t id) {
    if (id >= static_cast<std::uint16_t>(bedrocktools::memory::SignatureId::Count)) return 0;
    return bedrocktools::memory::resolve(static_cast<bedrocktools::memory::SignatureId>(id));
}

bedrocktools::sdk::ClientInstance* clientInstance() {
    return reinterpret_cast<bedrocktools::sdk::ClientInstance*>(bedrocktools::core::gamehooks::clientInstance());
}

std::uint64_t subscribe(bedrocktools::events::EventType type, bedrocktools::events::EventPriority priority, bedrocktools::api::EventCallback callback, void* userData) {
    if (!callback) return 0;
    return bedrocktools::events::bus().subscribeRaw(type, [type, callback, userData](void* payload) { callback(type, payload, userData); }, priority);
}

void unsubscribe(std::uint64_t subscription) {
    bedrocktools::events::bus().unsubscribe(subscription);
}

const bedrocktools::api::ApiV1 api{
    bedrocktools::api::AbiVersion,
    sizeof(bedrocktools::api::ApiV1),
    resolveSignature,
    clientInstance,
    subscribe,
    unsubscribe
};
}

extern "C" BEDROCKTOOLS_API const bedrocktools::api::ApiV1* BedrockTools_GetApi(std::uint32_t version) {
    return version == bedrocktools::api::AbiVersion ? &api : nullptr;
}
