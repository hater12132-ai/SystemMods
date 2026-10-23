#pragma once

namespace render_dispatch {

using RenderFn = void (*)(void* levelRenderer, void* screenContext, void* a3);

void add(RenderFn fn);
void ensureHooked();

}
