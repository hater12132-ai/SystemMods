#include "glintcolor.hpp"

#include "core/memory/Hooks.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/Types.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

using Color = bedrocktools::sdk::Color;

using SetEntityConstantsFn = void (*)(
    void*,
    void*,
    const Color*,
    const void*,
    const void*,
    const Color*,
    const Color*,
    const Color*,
    const Color*,
    const void*,
    const void*,
    float,
    float,
    float,
    float
);

using SetupActorShaderParametersGlintFn = void (*)(
    void*,
    void*,
    void*,
    const Color*,
    const Color*,
    const Color*,
    const Color*,
    float,
    float,
    float,
    float,
    const void*,
    const void*,
    float,
    std::uint8_t,
    const void*
);

using SetupFoilShaderParametersFn = void (*)(
    void*,
    const Color*,
    const Color*,
    const Color*,
    const void*
);

using SetupShaderParametersGlintFn = void (*)(
    void*,
    const Color*,
    const Color*,
    const Color*,
    const Color*,
    float,
    float,
    float,
    float,
    const void*,
    const void*,
    float
);

GlintColorModule* g_glintColor = nullptr;
SetEntityConstantsFn g_setEntityConstantsOriginal = nullptr;
SetupActorShaderParametersGlintFn g_setupActorShaderParametersGlintOriginal = nullptr;
SetupFoilShaderParametersFn g_setupFoilShaderParametersOriginal = nullptr;
SetupShaderParametersGlintFn g_setupShaderParametersGlintOriginal = nullptr;

Color makeGlintColor() {
    if (!g_glintColor) return {};

    const std::uint32_t color = g_glintColor->glintColor();
    const float opacity = std::clamp(g_glintColor->glintOpacity(), 0.0f, 1.0f);
    const float r = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(color & 0xFFu) / 255.0f;

    return {r * opacity, g * opacity, b * opacity, opacity};
}

void applyScreenGlintColor(void* screenContext) {
    if (!screenContext || !g_glintColor || !g_glintColor->enabled) return;

    using namespace bedrocktools::sdk::offsets;

    const auto constants = bedrocktools::sdk::field<std::uintptr_t>(
        screenContext,
        ScreenContext::mActorShaderConstants
    );
    if (!constants) return;

    const auto glintConstant = bedrocktools::sdk::field<std::uintptr_t>(
        reinterpret_cast<void*>(constants),
        ActorShaderConstants::mGlintColor
    );
    if (!glintConstant) return;

    const auto data = bedrocktools::sdk::field<std::uintptr_t>(
        reinterpret_cast<void*>(glintConstant),
        ShaderConstant::mData
    );
    if (!data) return;

    *reinterpret_cast<Color*>(data) = makeGlintColor();
    bedrocktools::sdk::field<std::uint8_t>(
        reinterpret_cast<void*>(glintConstant),
        ShaderConstant::mDirty
    ) = 1;
}

void setEntityConstantsHook(
    void* entityConstants,
    void* renderContext,
    const Color* tileLightColor,
    const void* tileLightColorUV,
    const void* blockLightColor,
    const Color* overlay,
    const Color* changeColor,
    const Color* changeColor2,
    const Color* glintColor,
    const void* glintUVScale,
    const void* uvAnim,
    float uvOffset1,
    float uvOffset2,
    float uvRot1,
    float uvRot2
) {
    if (!g_setEntityConstantsOriginal) return;

    if (g_glintColor && g_glintColor->enabled) {
        const Color custom = makeGlintColor();
        g_setEntityConstantsOriginal(
            entityConstants,
            renderContext,
            tileLightColor,
            tileLightColorUV,
            blockLightColor,
            overlay,
            changeColor,
            changeColor2,
            &custom,
            glintUVScale,
            uvAnim,
            uvOffset1,
            uvOffset2,
            uvRot1,
            uvRot2
        );
        return;
    }

    g_setEntityConstantsOriginal(
        entityConstants,
        renderContext,
        tileLightColor,
        tileLightColorUV,
        blockLightColor,
        overlay,
        changeColor,
        changeColor2,
        glintColor,
        glintUVScale,
        uvAnim,
        uvOffset1,
        uvOffset2,
        uvRot1,
        uvRot2
    );
}

void setupActorShaderParametersGlintHook(
    void* screenContext,
    void* entityContext,
    void* actor,
    const Color* overlay,
    const Color* changeColor,
    const Color* changeColor2,
    const Color* glintColor,
    float uvOffset1,
    float uvOffset2,
    float uvRot1,
    float uvRot2,
    const void* glintUVScale,
    const void* uvAnim,
    float br,
    std::uint8_t lightEmission,
    const void* lightEmissionColor
) {
    if (!g_setupActorShaderParametersGlintOriginal) return;

    if (g_glintColor && g_glintColor->enabled) {
        const Color custom = makeGlintColor();
        g_setupActorShaderParametersGlintOriginal(
            screenContext,
            entityContext,
            actor,
            overlay,
            changeColor,
            changeColor2,
            &custom,
            uvOffset1,
            uvOffset2,
            uvRot1,
            uvRot2,
            glintUVScale,
            uvAnim,
            br,
            lightEmission,
            lightEmissionColor
        );
        return;
    }

    g_setupActorShaderParametersGlintOriginal(
        screenContext,
        entityContext,
        actor,
        overlay,
        changeColor,
        changeColor2,
        glintColor,
        uvOffset1,
        uvOffset2,
        uvRot1,
        uvRot2,
        glintUVScale,
        uvAnim,
        br,
        lightEmission,
        lightEmissionColor
    );
}

void setupFoilShaderParametersHook(
    void* screenContext,
    const Color* overlay,
    const Color* changeColor,
    const Color* changeColor2,
    const void* uvScale
) {
    if (!g_setupFoilShaderParametersOriginal) return;
    g_setupFoilShaderParametersOriginal(screenContext, overlay, changeColor, changeColor2, uvScale);
    applyScreenGlintColor(screenContext);
}

void setupShaderParametersGlintHook(
    void* screenContext,
    const Color* overlay,
    const Color* changeColor,
    const Color* changeColor2,
    const Color* glintColor,
    float uvOffset1,
    float uvOffset2,
    float uvRot1,
    float uvRot2,
    const void* glintUVScale,
    const void* uvAnim,
    float br
) {
    if (!g_setupShaderParametersGlintOriginal) return;

    if (g_glintColor && g_glintColor->enabled) {
        const Color custom = makeGlintColor();
        g_setupShaderParametersGlintOriginal(
            screenContext,
            overlay,
            changeColor,
            changeColor2,
            &custom,
            uvOffset1,
            uvOffset2,
            uvRot1,
            uvRot2,
            glintUVScale,
            uvAnim,
            br
        );
        return;
    }

    g_setupShaderParametersGlintOriginal(
        screenContext,
        overlay,
        changeColor,
        changeColor2,
        glintColor,
        uvOffset1,
        uvOffset2,
        uvRot1,
        uvRot2,
        glintUVScale,
        uvAnim,
        br
    );
}

}

GlintColorModule::GlintColorModule()
    : Module("Glint Color", "Customizes the color of the enchantment glint effect.") {
    g_glintColor = this;
}

GlintColorModule::~GlintColorModule() {
    if (g_glintColor == this) g_glintColor = nullptr;
}

void GlintColorModule::onInit() {
    const auto entityAddress = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::ActorShaderManagerSetEntityConstants
    );
    const auto actorGlintAddress = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::ActorShaderManagerSetupShaderParametersActorGlint
    );
    const auto foilAddress = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::ActorShaderManagerSetupFoilShaderParameters
    );
    const auto uiAddress = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::ActorShaderManagerSetupShaderParametersGlint
    );

    m_entityTarget = reinterpret_cast<void*>(entityAddress);
    m_actorGlintTarget = reinterpret_cast<void*>(actorGlintAddress);
    m_foilTarget = reinterpret_cast<void*>(foilAddress);
    m_uiTarget = reinterpret_cast<void*>(uiAddress);
    installHooks();
}

void GlintColorModule::installHooks() {
    if (!m_entityHooked && m_entityTarget) {
        const auto hook = bedrocktools::hooks::install(
            m_entityTarget,
            reinterpret_cast<void*>(&setEntityConstantsHook),
            reinterpret_cast<void**>(&g_setEntityConstantsOriginal)
        );
        m_entityHooked = hook != nullptr;
    }

    if (!m_actorGlintHooked && m_actorGlintTarget) {
        const auto hook = bedrocktools::hooks::install(
            m_actorGlintTarget,
            reinterpret_cast<void*>(&setupActorShaderParametersGlintHook),
            reinterpret_cast<void**>(&g_setupActorShaderParametersGlintOriginal)
        );
        m_actorGlintHooked = hook != nullptr;
    }

    if (!m_foilHooked && m_foilTarget) {
        const auto hook = bedrocktools::hooks::install(
            m_foilTarget,
            reinterpret_cast<void*>(&setupFoilShaderParametersHook),
            reinterpret_cast<void**>(&g_setupFoilShaderParametersOriginal)
        );
        m_foilHooked = hook != nullptr;
    }

    if (!m_uiHooked && m_uiTarget) {
        const auto hook = bedrocktools::hooks::install(
            m_uiTarget,
            reinterpret_cast<void*>(&setupShaderParametersGlintHook),
            reinterpret_cast<void**>(&g_setupShaderParametersGlintOriginal)
        );
        m_uiHooked = hook != nullptr;
    }
}

void GlintColorModule::onEnable() {
    installHooks();
}

void GlintColorModule::onDisable() {
}

void GlintColorModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);

    if (j.contains("glintColor") && j["glintColor"].is_string()) {
        const std::string value = j["glintColor"].get<std::string>();
        if (!value.empty() && value[0] == '#') {
            try {
                m_glintColor.store(
                    static_cast<std::uint32_t>(std::stoul(value.substr(1), nullptr, 16)),
                    std::memory_order_relaxed
                );
            } catch (...) {
            }
        }
    }

    if (j.contains("glintOpacity")) {
        try {
            m_glintOpacity.store(
                std::clamp(j["glintOpacity"].get<float>(), 0.0f, 1.0f),
                std::memory_order_relaxed
            );
        } catch (...) {
        }
    }
}

void GlintColorModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);

    char color[10];
    std::snprintf(color, sizeof(color), "#%08X", m_glintColor.load(std::memory_order_relaxed));
    j["glintColor"] = std::string(color);
    j["glintOpacity"] = m_glintOpacity.load(std::memory_order_relaxed);
}

std::uint32_t GlintColorModule::glintColor() const {
    return m_glintColor.load(std::memory_order_relaxed);
}

float GlintColorModule::glintOpacity() const {
    return m_glintOpacity.load(std::memory_order_relaxed);
}
