#pragma once

#include "../Module.hpp"

class AutoGG : public Module {
public:
    static AutoGG* instance;
    
    AutoGG();
    
    void onInit() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    
    std::string ggMessage = "gg";
};
