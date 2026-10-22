#include "ModuleRegistry.hpp"
#include "visual/fullbright.hpp"
#include "visual/fogcolor.hpp"
#include "visual/glintcolor.hpp"
#include "visual/nofog.hpp"
#include "hud/armorhud.hpp"
#include "player/timechanger.hpp"
#include "player/weatherchanger.hpp"
#include "player/nick.hpp"
#include "misc/nodisconnect.hpp"
#include "visual/viewmodel.hpp"
#include "visual/thirdpersonnametag.hpp"
#include "visual/hitbox.hpp"
#include "visual/connectedglass.hpp"
#include "visual/swingmodifier.hpp"

ModuleRegistry& ModuleRegistry::get() {
    static ModuleRegistry registry;
    return registry;
}

Module* ModuleRegistry::find(std::string_view id) const {
    const auto it = mById.find(id);
    return it == mById.end() ? nullptr : it->second;
}

const std::vector<Module*>& ModuleRegistry::modules() const {
    return mView;
}

void ModuleRegistry::initialize() {
    if (mInitialized) return;
    for (auto* module : mView) module->onInit();
    mInitialized = true;
}

void ModuleRegistry::onFrame() {
    for (auto* module : mView) if (module->enabled) module->onFrame();
}

bool ModuleRegistry::onMouseEvent(int button, bool isDown) {
    bool consumed = false;
    for (auto* module : mView) if (module->onMouseEvent(button, isDown)) consumed = true;
    return consumed;
}

void ModuleRegistry::setKeybindBlocked(bool blocked) {
    mKeybindBlocked = blocked;
}

bool ModuleRegistry::keybindBlocked() const {
    return mKeybindBlocked;
}

void registerAllModules() {
    auto& registry = ModuleRegistry::get();
    if (!registry.modules().empty()) return;

    registry.emplace<ArmorHudModule>();
    registry.emplace<ConnectedGlassModule>();
    registry.emplace<FogColorModule>();
    registry.emplace<FullbrightModule>();
    registry.emplace<GlintColorModule>();
    registry.emplace<HitboxModule>();
    registry.emplace<NoFogModule>();
    registry.emplace<SwingModifierModule>();
    registry.emplace<ThirdPersonNametagModule>();
    registry.emplace<ViewModelModule>();
    registry.emplace<NickModule>();
    registry.emplace<TimeChangerModule>();
    registry.emplace<WeatherChangerModule>();

    auto& noDc = registry.emplace<NoDisconnectModule>();
    noDc.showInMenu = false;
    noDc.setMasterEnabled(true);
}
