#pragma once

#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/render/LevelRendererPlayer.hpp>

namespace bedrocktools::sdk {

class LevelRenderer {
public:
    void* renderChunkCoordinatorTable() {
        return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + offsets::LevelRenderer::mRenderChunkCoordinators);
    }

    LevelRendererPlayer* playerRenderer() { return field<LevelRendererPlayer*>(this, offsets::LevelRenderer::mLevelRendererPlayer); }
};

}
