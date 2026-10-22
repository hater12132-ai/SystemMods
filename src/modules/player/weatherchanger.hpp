#pragma once

#include "../Module.hpp"

class WeatherChangerModule : public Module {
public:
    enum class WeatherMode : int { Clear = 0, Rain = 1, Thunder = 2, Snow = 3 };

    WeatherChangerModule();
    ~WeatherChangerModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    void installHooks();
    void applyWeather(void* weather) const;
    float getRainLevel() const;
    float getLightningLevel() const;

    WeatherMode m_mode = WeatherMode::Clear;

private:
    bool m_hooked = false;
};
