#pragma once

#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/Types.hpp>

namespace bedrocktools::sdk {

class LevelRendererPlayer {
public:
    float& fogColorRed() { return field<float>(this, offsets::LevelRendererPlayer::mFogColorRed); }
    float& fogColorGreen() { return field<float>(this, offsets::LevelRendererPlayer::mFogColorGreen); }
    float& fogColorBlue() { return field<float>(this, offsets::LevelRendererPlayer::mFogColorBlue); }
    float& baseFogStart() { return field<float>(this, offsets::LevelRendererPlayer::mBaseFogStart); }
    float& baseFogEnd() { return field<float>(this, offsets::LevelRendererPlayer::mBaseFogEnd); }
    float& currentFogDensityMax() { return field<float>(this, offsets::LevelRendererPlayer::mCurrentFogDensityMax); }
    Vec3& cameraPosition() { return field<Vec3>(this, offsets::LevelRendererPlayer::mCamPos); }
    void*& selectionOverlayMaterial() { return field<void*>(this, offsets::LevelRendererPlayer::mSelectionOverlayMaterial); }
};

}
