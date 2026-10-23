#pragma once

#include <bedrocktools/sdk/Types.hpp>
#include <cstring>
#include <cmath>

namespace fakeplayer {

struct Instance {
    bool active = false;
    bedrocktools::sdk::Vec3 feet{0, 0, 0};
    float yaw = 0.f;
    float width = 0.6f;
    float height = 1.8f;
    float hitFlash = 0.f;      // 0..1
    float lifeTime = 0.f;      // seconds since spawn
    int hitCount = 0;
    char name[64]{"FakePlayer"};

    bedrocktools::sdk::Vec3 center() const {
        return {feet.x, feet.y + height * 0.5f, feet.z};
    }

    void aabb(float& minx, float& miny, float& minz,
              float& maxx, float& maxy, float& maxz) const {
        float hw = width * 0.5f;
        minx = feet.x - hw; maxx = feet.x + hw;
        miny = feet.y;      maxy = feet.y + height;
        minz = feet.z - hw; maxz = feet.z + hw;
    }
};

inline Instance& get() {
    static Instance inst;
    return inst;
}

inline void setName(const char* n) {
    auto& i = get();
    if (!n || !n[0]) n = "FakePlayer";
    std::strncpy(i.name, n, sizeof(i.name) - 1);
    i.name[sizeof(i.name) - 1] = 0;
}

} // namespace fakeplayer
