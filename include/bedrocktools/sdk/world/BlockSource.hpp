#pragma once

#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>

namespace bedrocktools::sdk {

class BlockSource {
public:
    int dimensionId() {
        return virtualCall<int>(this, offsets::VTable::BlockSource_getDimensionId);
    }
};

}
