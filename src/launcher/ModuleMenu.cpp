#include "ModuleMenu.hpp"
#include "modules/ModuleRegistry.hpp"

// SystemClient mode: modules are controlled only by the in-game SystemDLC overlay.
// Do not register individual entries into Levi's PL mod menu (that feels like plugins).
void registerModulesWithLauncher() {
    (void)ModuleRegistry::get();
}
