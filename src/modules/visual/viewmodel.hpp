#pragma once

#include "../Module.hpp"

class ViewModelModule : public Module {
public:
    ViewModelModule();
    ~ViewModelModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    float getItemFov() const;
    bool  isThirdPerson() const;

    float m_posX = 0.0f, m_posY = 0.0f, m_posZ = 0.0f;
    float m_rotX = 0.0f, m_rotY = 0.0f, m_rotZ = 0.0f;
    float m_scaleX = 1.0f, m_scaleY = 1.0f, m_scaleZ = 1.0f;
    float m_pivotX = 0.0f, m_pivotY = 0.0f, m_pivotZ = 0.0f;
    float m_itemFov = 70.0f;
    bool  m_applyThirdPerson = false;
    bool  m_thirdPerson = false;

private:
    bool m_renderItemHooked = false;
    bool m_fovHooked = false;
    bool m_perspectiveHooked = false;
};
