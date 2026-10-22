#include "cpslimiter.hpp"
#include <algorithm>

namespace {
constexpr int kMouseLeft = 1;
constexpr int kMouseRight = 2;
constexpr int kMinCps = 1;
constexpr int kMaxCps = 30;
}

CpsLimiterModule::CpsLimiterModule()
    : Module("CPS Limiter", "Limits left and right mouse clicks before they reach Minecraft.") {
}

void CpsLimiterModule::onEnable() {
    m_active.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_stateMutex);
    resetRateState(m_leftState);
    resetRateState(m_rightState);
}

void CpsLimiterModule::onDisable() {
    m_active.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_stateMutex);
    resetRateState(m_leftState);
    resetRateState(m_rightState);
}

bool CpsLimiterModule::onMouseEvent(int button, bool isDown) {
    ButtonState* state = nullptr;
    bool limited = false;
    int cps = kMaxCps;

    if (button == kMouseLeft) {
        state = &m_leftState;
        limited = m_limitLeft.load(std::memory_order_relaxed);
        cps = m_limitLeftCps.load(std::memory_order_relaxed);
    } else if (button == kMouseRight) {
        state = &m_rightState;
        limited = m_limitRight.load(std::memory_order_relaxed);
        cps = m_limitRightCps.load(std::memory_order_relaxed);
    } else {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);
    return handleButton(*state, isDown, limited && m_active.load(std::memory_order_acquire), cps);
}

bool CpsLimiterModule::handleButton(ButtonState& state, bool isDown, bool limited, int cps) {
    if (!isDown) {
        if (!state.physicalDown) return false;
        state.physicalDown = false;
        const bool consume = !state.forwardedDown;
        state.forwardedDown = false;
        return consume;
    }

    if (state.physicalDown) return !state.forwardedDown;
    state.physicalDown = true;

    if (!limited) {
        state.forwardedDown = true;
        return false;
    }

    cps = std::clamp(cps, kMinCps, kMaxCps);
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - std::chrono::seconds(1);
    while (!state.allowedClicks.empty() && state.allowedClicks.front() <= cutoff) {
        state.allowedClicks.pop_front();
    }

    const auto interval = std::chrono::nanoseconds(1000000000LL / cps);
    if (now < state.nextAllowed || static_cast<int>(state.allowedClicks.size()) >= cps) {
        state.forwardedDown = false;
        return true;
    }

    state.forwardedDown = true;
    state.nextAllowed = now + interval;
    state.allowedClicks.push_back(now);
    return false;
}

void CpsLimiterModule::resetRateState(ButtonState& state) {
    state.physicalDown = false;
    state.forwardedDown = false;
    state.nextAllowed = {};
    state.allowedClicks.clear();
}

void CpsLimiterModule::loadConfig(const nlohmann::json& j) {
    if (j.contains("m_limitLeft")) m_limitLeft.store(j["m_limitLeft"].get<bool>(), std::memory_order_relaxed);
    if (j.contains("m_limitLeftCps")) m_limitLeftCps.store(std::clamp(j["m_limitLeftCps"].get<int>(), kMinCps, kMaxCps), std::memory_order_relaxed);
    if (j.contains("m_limitRight")) m_limitRight.store(j["m_limitRight"].get<bool>(), std::memory_order_relaxed);
    if (j.contains("m_limitRightCps")) m_limitRightCps.store(std::clamp(j["m_limitRightCps"].get<int>(), kMinCps, kMaxCps), std::memory_order_relaxed);
    Module::loadConfig(j);
}

void CpsLimiterModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["m_limitLeft"] = m_limitLeft.load(std::memory_order_relaxed);
    j["m_limitLeftCps"] = m_limitLeftCps.load(std::memory_order_relaxed);
    j["m_limitRight"] = m_limitRight.load(std::memory_order_relaxed);
    j["m_limitRightCps"] = m_limitRightCps.load(std::memory_order_relaxed);
}
