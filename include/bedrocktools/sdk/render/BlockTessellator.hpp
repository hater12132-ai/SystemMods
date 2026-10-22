#pragma once

#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/world/BlockSource.hpp>

namespace bedrocktools::sdk {

class BlockTessellator {
public:
    BlockSource* region() { return field<BlockSource*>(this, offsets::BlockTessellator::mRegion); }
    bool usesInternalTexture() const { return field<std::uint8_t>(this, offsets::BlockTessellator::mUseInternalTexture) != 0; }
    std::uint8_t& xFlipTexture() { return field<std::uint8_t>(this, offsets::BlockTessellator::mXFlipTexture); }
    void* internalTexture() { return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + offsets::BlockTessellator::mInternalTexture); }
};

}
