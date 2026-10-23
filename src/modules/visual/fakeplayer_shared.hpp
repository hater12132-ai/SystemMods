#pragma once
#include <bedrocktools/sdk/Types.hpp>
#include <cstring>

namespace fakeplayer {

struct Instance {
    bool active = false;
    bedrocktools::sdk::Vec3 feet{0, 0, 0};
    float yaw = 0.f;
    float width = 0.6f;
    float height = 1.8f;
    float hitFlash = 0.f;
    char name[64]{"FakePlayer"};
};

inline Instance& get() {
    static Instance i;
    return i;
}

inline void setName(const char* n) {
    auto& i = get();
    if (!n || !n[0]) n = "FakePlayer";
    std::strncpy(i.name, n, sizeof(i.name) - 1);
    i.name[sizeof(i.name) - 1] = 0;
}

} // namespace fakeplayer
