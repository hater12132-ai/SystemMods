#pragma once

#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/world/BlockSource.hpp>
#include <bedrocktools/sdk/world/Weather.hpp>

namespace bedrocktools::sdk {

class Dimension {
public:
    BlockSource* blockSource() { return field<BlockSource*>(this, offsets::Dimension::mBlockSource); }
    Weather* weather() { return field<Weather*>(this, offsets::Dimension::mWeather); }
};

}
