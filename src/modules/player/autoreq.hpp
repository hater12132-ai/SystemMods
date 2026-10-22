#pragma once

#include "../Module.hpp"
#include <string>

class AutoReQ : public Module {
public:
    AutoReQ();
    ~AutoReQ() override;
    
    void onInit() override;
    
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;
    
    
    bool soloMode = true;
    bool teamElimination = true;
    bool gameOver = false;
    bool roleMurderer = false;
    bool roleSheriff = false;
    bool roleInnocent = false;
    bool roleHider = false;
    bool roleSeeker = false;
    bool roleDeath = false;
    bool roleRunner = false;

    int cooldownMs = 3000;

    static AutoReQ* instance;
};
