#pragma once

#include <bedrocktools/events/Event.hpp>

namespace bedrocktools::sdk { class ClientInstance; }

namespace bedrocktools::events {

struct ClientInstanceUpdateEvent {
    static constexpr EventType type = EventType::ClientInstanceUpdate;
    sdk::ClientInstance* clientInstance;
};

}
