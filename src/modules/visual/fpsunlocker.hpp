#pragma once

#include "../Module.hpp"
#include <EGL/egl.h>
#include <cstdint>
#include <time.h>

class FPSUnlockerModule : public Module {
public:
    FPSUnlockerModule();
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;

    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

private:
    int targetFPS = 0;

    struct timespec m_lastFrameTime = {};
    bool m_hasLastFrame = false;
    EGLDisplay m_appliedDisplay = EGL_NO_DISPLAY;
    EGLSurface m_appliedSurface = EGL_NO_SURFACE;
};
