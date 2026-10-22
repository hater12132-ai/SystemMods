#include "notouchborder.hpp"

#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include "core/memory/Hooks.hpp"

#include <cstddef>
#include <cstdint>

namespace {

struct RawVectorHeader {
    std::uintptr_t begin;
    std::uintptr_t end;
    std::uintptr_t capacity;
};

using EditorTickFn = void (*)(void*, void*, void*, int);
using EditorRenderFn = void (*)(const void*, void*);

EditorTickFn g_originalEditorTick = nullptr;
EditorRenderFn g_originalEditorRender = nullptr;
NoTouchBorderModule* g_noTouchBorderModule = nullptr;

class ScopedEmptyReservedAreas {
public:
    explicit ScopedEmptyReservedAreas(void* self) {
        if (!self) return;

        m_vector = reinterpret_cast<RawVectorHeader*>(
            reinterpret_cast<std::uintptr_t>(self) +
            bedrocktools::sdk::offsets::ControlOptionEditorControl::mReservedAreas
        );

        m_savedBegin = m_vector->begin;
        m_savedEnd = m_vector->end;
        m_savedCapacity = m_vector->capacity;

        if (m_savedBegin == 0 && m_savedEnd == 0 && m_savedCapacity == 0) {
            m_vector = nullptr;
            return;
        }

        if (m_savedBegin == 0 ||
            m_savedEnd < m_savedBegin ||
            m_savedCapacity < m_savedEnd ||
            ((m_savedEnd - m_savedBegin) %
             bedrocktools::sdk::offsets::ControlOptionEditorControl::ReservedAreaEntrySize) != 0) {
            m_vector = nullptr;
            return;
        }

        m_vector->end = m_savedBegin;
        m_active = true;
    }

    ~ScopedEmptyReservedAreas() {
        if (!m_active || !m_vector) return;

        if (m_vector->begin == m_savedBegin &&
            m_vector->capacity == m_savedCapacity &&
            m_vector->end == m_savedBegin) {
            m_vector->end = m_savedEnd;
        }
    }

    ScopedEmptyReservedAreas(const ScopedEmptyReservedAreas&) = delete;
    ScopedEmptyReservedAreas& operator=(const ScopedEmptyReservedAreas&) = delete;

private:
    RawVectorHeader* m_vector = nullptr;
    std::uintptr_t m_savedBegin = 0;
    std::uintptr_t m_savedEnd = 0;
    std::uintptr_t m_savedCapacity = 0;
    bool m_active = false;
};

void editorTickHook(
    void* self,
    void* inputEventQueue,
    void* touchPointResults,
    int value
) {
    if (!g_originalEditorTick) return;

    if (!g_noTouchBorderModule || !g_noTouchBorderModule->enabled) {
        g_originalEditorTick(self, inputEventQueue, touchPointResults, value);
        return;
    }

    ScopedEmptyReservedAreas hideReservedAreas(self);
    g_originalEditorTick(self, inputEventQueue, touchPointResults, value);
}

void editorRenderHook(const void* self, void* inputRenderContext) {
    if (!g_originalEditorRender) return;

    if (!g_noTouchBorderModule || !g_noTouchBorderModule->enabled) {
        g_originalEditorRender(self, inputRenderContext);
        return;
    }

    ScopedEmptyReservedAreas hideReservedAreas(const_cast<void*>(self));
    g_originalEditorRender(self, inputRenderContext);
}

}

NoTouchBorderModule::NoTouchBorderModule()
    : Module(
          "No Touch Border",
          "Removes reserved red zones from touch-control customization.") {
    g_noTouchBorderModule = this;
}

void NoTouchBorderModule::onInit() {
    const std::uintptr_t tickAddress =
        bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ControlOptionEditorTick);
    const std::uintptr_t renderAddress =
        bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ControlOptionEditorRender);

    if (tickAddress != 0 && !g_originalEditorTick) {
        bedrocktools::hooks::install(
            reinterpret_cast<void*>(tickAddress),
            reinterpret_cast<void*>(editorTickHook),
            reinterpret_cast<void**>(&g_originalEditorTick)
        );
    }

    if (renderAddress != 0 && !g_originalEditorRender) {
        bedrocktools::hooks::install(
            reinterpret_cast<void*>(renderAddress),
            reinterpret_cast<void*>(editorRenderHook),
            reinterpret_cast<void**>(&g_originalEditorRender)
        );
    }
}
