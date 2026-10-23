#pragma once

#include <bedrocktools/sdk/Types.hpp>
#include <cstring>

namespace fakeplayer {

struct Instance {
    bool active = false;
    bedrocktools::sdk::Vec3 feet{0, 0, 0}; // bottom-center
    float yaw = 0.f;                       // degrees
    float width = 0.6f;
    float height = 1.8f;
    float hitFlash = 0.f;                  // 0..1 visual hit feedback
    char name[64]{"FakePlayer"};

    bedrocktools::sdk::Vec3 center() const {
        return {feet.x, feet.y + height * 0.5f, feet.z};
    }
};

inline Instance& get() {
    static Instance inst;
    return inst;
}

inline void setName(const char* n) {
    auto& i = get();
    if (!n) n = "FakePlayer";
    std::strncpy(i.name, n, sizeof(i.name) - 1);
    i.name[sizeof(i.name) - 1] = 0;
}

} // namespace fakeplayer
