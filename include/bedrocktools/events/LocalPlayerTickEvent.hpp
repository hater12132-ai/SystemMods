#pragma once

#include <bedrocktools/events/Event.hpp>

namespace bedrocktools::sdk { class Player; }

namespace bedrocktools::events {

struct LocalPlayerTickEvent {
    static constexpr EventType type = EventType::LocalPlayerTick;
    sdk::Player* player;
};

}
