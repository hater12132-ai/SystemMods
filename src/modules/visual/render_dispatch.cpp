#include "render_dispatch.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include "core/memory/Hooks.hpp"
#include <mutex>
#include <vector>

namespace render_dispatch {

static std::mutex g_mu;
static std::vector<RenderFn> g_fns;
static bool g_hooked = false;
static void (*g_orig)(void*, void*, void*) = nullptr;

static void hook(void* levelRenderer, void* screenContext, void* a3) {
    if (g_orig) g_orig(levelRenderer, screenContext, a3);
    std::vector<RenderFn> copy;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        copy = g_fns;
    }
    for (auto* fn : copy) if (fn) fn(levelRenderer, screenContext, a3);
}

void add(RenderFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(g_mu);
    for (auto* e : g_fns) if (e == fn) return;
    g_fns.push_back(fn);
}

void ensureHooked() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_hooked) return;
    auto addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderLevel);
    if (!addr) return;
    bedrocktools::hooks::install((void*)addr, (void*)hook, (void**)&g_orig);
    g_hooked = true;
}

} // namespace render_dispatch
