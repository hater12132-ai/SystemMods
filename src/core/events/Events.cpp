#include <bedrocktools/events/EventBus.hpp>

namespace bedrocktools::events {

EventBus& bus() {
    static EventBus instance;
    return instance;
}

}
