#include "ModuleRegistry.hpp"
#include "visual/targetesp.hpp"
#include "visual/fakeplayer.hpp"

ModuleRegistry& ModuleRegistry::get() {
    static ModuleRegistry registry;
    return registry;
}
Module* ModuleRegistry::find(std::string_view id) const {
    const auto it = mById.find(id);
    return it == mById.end() ? nullptr : it->second;
}
const std::vector<Module*>& ModuleRegistry::modules() const { return mView; }
void ModuleRegistry::initialize() {
    if (mInitialized) return;
    for (auto* m : mView) m->onInit();
    mInitialized = true;
}
void ModuleRegistry::onFrame() {
    for (auto* m : mView) if (m->enabled) m->onFrame();
}
bool ModuleRegistry::onMouseEvent(int button, bool isDown) {
    bool c = false;
    for (auto* m : mView) if (m->onMouseEvent(button, isDown)) c = true;
    return c;
}
void ModuleRegistry::setKeybindBlocked(bool blocked) { mKeybindBlocked = blocked; }
bool ModuleRegistry::keybindBlocked() const { return mKeybindBlocked; }

void registerAllModules() {
    auto& r = ModuleRegistry::get();
    if (!r.modules().empty()) return;
    r.emplace<TargetESPModule>();
    r.emplace<FakePlayerModule>();
}
