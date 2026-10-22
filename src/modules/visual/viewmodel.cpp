#include "viewmodel.hpp"
#include "core/memory/Hooks.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>

static ViewModelModule* g_viewModelMod = nullptr;


static void (*_renderItem_orig)(void*, void*, void*, void*, int, int, int, int) = nullptr;

static void _renderItem_hook(void* _this, void* renderContext, void* entity,
                             void* item, int posAndRotSet, int itemFlags,
                             int useMatrixAsIs, int renderingMainHand)
{
    if (g_viewModelMod && g_viewModelMod->enabled) {
        bool skip = g_viewModelMod->isThirdPerson() && !g_viewModelMod->m_applyThirdPerson;

        if (!skip) {
            uintptr_t rcBase = (uintptr_t)renderContext;
            uintptr_t ptr1 = *(uintptr_t*)(rcBase + bedrocktools::sdk::offsets::RenderContext::mMatrixStackWrapper);
            if (ptr1 != 0) {
                uintptr_t matStack = *(uintptr_t*)(ptr1 + bedrocktools::sdk::offsets::MatrixStackWrapper::mMatrixStack);
                if (matStack != 0) {
                    uintptr_t* blocks = *(uintptr_t**)(matStack + bedrocktools::sdk::offsets::MatrixStack::mBlocks);
                    size_t start = *(size_t*)(matStack + bedrocktools::sdk::offsets::MatrixStack::mStart);
                    size_t size  = *(size_t*)(matStack + bedrocktools::sdk::offsets::MatrixStack::mSize);

                    if (blocks != nullptr && size > 0) {
                        size_t last = start + size - 1;
                        size_t blockOff = (last >> 3) & ~(size_t)7; 
                        size_t elemIdx  = last & 0x3F;              
                        uintptr_t blockPtr = *(uintptr_t*)((uintptr_t)blocks + blockOff);

                        if (blockPtr != 0) {
                            glm::mat4& matrix = *(glm::mat4*)(blockPtr + elemIdx * 64);

                            float px = g_viewModelMod->m_posX;
                            float py = g_viewModelMod->m_posY;
                            float pz = g_viewModelMod->m_posZ;
                            if (px != 0.0f || py != 0.0f || pz != 0.0f) {
                                matrix = glm::translate(matrix, glm::vec3(px, py, pz));
                            }

                            float rx = g_viewModelMod->m_rotX;
                            float ry = g_viewModelMod->m_rotY;
                            float rz = g_viewModelMod->m_rotZ;
                            bool hasRotation = (rx != 0.0f || ry != 0.0f || rz != 0.0f);
                            if (hasRotation) {
                                float pivX = g_viewModelMod->m_pivotX;
                                float pivY = g_viewModelMod->m_pivotY;
                                float pivZ = g_viewModelMod->m_pivotZ;
                                bool hasPivot = (pivX != 0.0f || pivY != 0.0f || pivZ != 0.0f);

                                if (hasPivot)
                                    matrix = glm::translate(matrix, glm::vec3(pivX, pivY, pivZ));

                                if (rx != 0.0f)
                                    matrix = glm::rotate(matrix, glm::radians(rx), glm::vec3(1.0f, 0.0f, 0.0f));
                                if (ry != 0.0f)
                                    matrix = glm::rotate(matrix, glm::radians(ry), glm::vec3(0.0f, 1.0f, 0.0f));
                                if (rz != 0.0f)
                                    matrix = glm::rotate(matrix, glm::radians(rz), glm::vec3(0.0f, 0.0f, 1.0f));

                                if (hasPivot)
                                    matrix = glm::translate(matrix, glm::vec3(-pivX, -pivY, -pivZ));
                            }

                            float sx = g_viewModelMod->m_scaleX;
                            float sy = g_viewModelMod->m_scaleY;
                            float sz = g_viewModelMod->m_scaleZ;
                            if (sx != 1.0f || sy != 1.0f || sz != 1.0f) {
                                matrix = glm::scale(matrix, glm::vec3(sx, sy, sz));
                            }
                        }
                    }
                }
            }
        }
    }

    if (_renderItem_orig)
        _renderItem_orig(_this, renderContext, entity, item, posAndRotSet,
                         itemFlags, useMatrixAsIs, renderingMainHand);
}


static float (*_getFov_orig)(void*, float, int) = nullptr;

static float _getFov_hook(void* _this, float a, int enableVariableFOV) {
    float result = 0.0f;
    if (_getFov_orig)
        result = _getFov_orig(_this, a, enableVariableFOV);

    if (g_viewModelMod && g_viewModelMod->enabled) {
        if (result >= 69.5f && result <= 70.5f) {
            result = g_viewModelMod->getItemFov();
        }
    }
    return result;
}


static int (*_getPerspective_orig)(void*) = nullptr;

static int _getPerspective_hook(void* _this) {
    int result = 0;
    if (_getPerspective_orig)
        result = _getPerspective_orig(_this);

    if (g_viewModelMod && g_viewModelMod->enabled) {
        g_viewModelMod->m_thirdPerson = (result != 0);
    }
    return result;
}


ViewModelModule::ViewModelModule()
    : Module("View Model", "Modify your held item's position, rotation, scale and FOV.") {
    g_viewModelMod = this;
}

ViewModelModule::~ViewModelModule() {
    if (g_viewModelMod == this) g_viewModelMod = nullptr;
}

float ViewModelModule::getItemFov() const {
    return m_itemFov;
}

bool ViewModelModule::isThirdPerson() const {
    return m_thirdPerson;
}

void ViewModelModule::onInit() {
    if (!m_renderItemHooked) {
        uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderItem);
        if (addr != 0) {
            bedrocktools::hooks::install((void*)addr, (void*)_renderItem_hook, (void**)&_renderItem_orig);
            m_renderItemHooked = true;
        }
    }

    if (!m_fovHooked) {
        uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::GetFov);
        if (addr != 0) {
            bedrocktools::hooks::install((void*)addr, (void*)_getFov_hook, (void**)&_getFov_orig);
            m_fovHooked = true;
        }
    }

    if (!m_perspectiveHooked) {
        uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::GetPerspective);
        if (addr != 0) {
            bedrocktools::hooks::install((void*)addr, (void*)_getPerspective_hook, (void**)&_getPerspective_orig);
            m_perspectiveHooked = true;
        }
    }
}

void ViewModelModule::onEnable() {
}

void ViewModelModule::onDisable() {
}

void ViewModelModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("m_posX"))    m_posX    = j["m_posX"].get<float>();
    if (j.contains("m_posY"))    m_posY    = j["m_posY"].get<float>();
    if (j.contains("m_posZ"))    m_posZ    = j["m_posZ"].get<float>();
    if (j.contains("m_rotX"))    m_rotX    = j["m_rotX"].get<float>();
    if (j.contains("m_rotY"))    m_rotY    = j["m_rotY"].get<float>();
    if (j.contains("m_rotZ"))    m_rotZ    = j["m_rotZ"].get<float>();
    if (j.contains("m_scaleX"))  m_scaleX  = j["m_scaleX"].get<float>();
    if (j.contains("m_scaleY"))  m_scaleY  = j["m_scaleY"].get<float>();
    if (j.contains("m_scaleZ"))  m_scaleZ  = j["m_scaleZ"].get<float>();
    if (j.contains("m_pivotX"))  m_pivotX  = j["m_pivotX"].get<float>();
    if (j.contains("m_pivotY"))  m_pivotY  = j["m_pivotY"].get<float>();
    if (j.contains("m_pivotZ"))  m_pivotZ  = j["m_pivotZ"].get<float>();
    if (j.contains("m_itemFov")) m_itemFov = j["m_itemFov"].get<float>();
    if (j.contains("m_applyThirdPerson")) m_applyThirdPerson = j["m_applyThirdPerson"].get<bool>();
}

void ViewModelModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["m_posX"]    = m_posX;
    j["m_posY"]    = m_posY;
    j["m_posZ"]    = m_posZ;
    j["m_rotX"]    = m_rotX;
    j["m_rotY"]    = m_rotY;
    j["m_rotZ"]    = m_rotZ;
    j["m_scaleX"]  = m_scaleX;
    j["m_scaleY"]  = m_scaleY;
    j["m_scaleZ"]  = m_scaleZ;
    j["m_pivotX"]  = m_pivotX;
    j["m_pivotY"]  = m_pivotY;
    j["m_pivotZ"]  = m_pivotZ;
    j["m_itemFov"] = m_itemFov;
    j["m_applyThirdPerson"] = m_applyThirdPerson;
}
