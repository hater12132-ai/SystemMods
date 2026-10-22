#pragma once

#include "../Module.hpp"
#include <string>

class NickModule : public Module {
public:
    NickModule();
    ~NickModule() override;
    
    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    std::string m_fakeName = "Levi User";
    bool m_bold = false;
    bool m_italic = true;
    bool m_obfuscated = false;
    uint32_t m_textColor = 0xFFFFFFFF;

    std::string m_originalName;
    std::string m_originalNametag;
    std::string m_backupNametag;
    
private:
    bool m_tickHooked = false;
    bool m_drawTextHooked = false;
};
