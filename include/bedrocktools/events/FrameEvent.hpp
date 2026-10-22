#pragma once

#include <bedrocktools/events/Event.hpp>

namespace bedrocktools::events {

struct FrameEvent {
    static constexpr EventType type = EventType::Frame;
};

}
