#include "tnttimer.hpp"

#include "core/memory/Hooks.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Offsets.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>

namespace {
using PrimedTntNormalTickFn = void (*)(void*);
using ActorGetNameTagFn = std::string (*)(void*);
using ActorSetNameTagFn = void (*)(void*, const std::string&);
using SynchedActorDataEnsureIndexFn = void (*)(void*, std::uint16_t);
using ActorSynchedDataUpdateAlwaysShowNameTagFn = void (*)(void*, const void*);

struct OriginalNametagState {
    std::string name;
    bool hadAlwaysShowItem = false;
    std::int8_t alwaysShowValue = 0;
};

TntTimerModule* g_tntTimer = nullptr;
PrimedTntNormalTickFn g_normalTickOriginal = nullptr;
ActorGetNameTagFn g_getNameTag = nullptr;
ActorSetNameTagFn g_setNameTag = nullptr;
SynchedActorDataEnsureIndexFn g_ensureIndex = nullptr;
ActorSynchedDataUpdateAlwaysShowNameTagFn g_updateAlwaysShowNameTag = nullptr;
std::unordered_map<void*, OriginalNametagState> g_originalStates;

void* getEntityDataWrapper(void* actor) {
    if (!actor) return nullptr;
    return reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(actor) + bedrocktools::sdk::offsets::Actor::mEntityData
    );
}

void* getEntityContext(void* actor) {
    if (!actor) return nullptr;
    return reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(actor) + bedrocktools::sdk::offsets::Actor::mEntityContext
    );
}

void* getDataComponent(void* actor) {
    void* wrapper = getEntityDataWrapper(actor);
    if (!wrapper) return nullptr;
    return *reinterpret_cast<void**>(wrapper);
}

void** getItemsBegin(void* component) {
    if (!component) return nullptr;
    return *reinterpret_cast<void***>(component);
}

std::size_t getItemsSize(void* component) {
    if (!component) return 0;
    auto** begin = *reinterpret_cast<void***>(component);
    auto** end = *reinterpret_cast<void***>(reinterpret_cast<std::uintptr_t>(component) + sizeof(void*));
    if (!begin || !end || end < begin) return 0;
    return static_cast<std::size_t>(end - begin);
}

void markDataItemPresentAndDirty(void* component, std::size_t id) {
    if (!component || id >= 192) return;

    auto* dirty = reinterpret_cast<std::uint64_t*>(
        reinterpret_cast<std::uintptr_t>(component) + 0x18
    );
    auto* present = reinterpret_cast<std::uint64_t*>(
        reinterpret_cast<std::uintptr_t>(component) + 0x30
    );

    const std::size_t word = id / 64;
    const std::uint64_t bit = std::uint64_t{1} << (id % 64);
    dirty[word] |= bit;
    present[word] |= bit;
}

void* findSCharDataItemVtable(void* component) {
    auto** begin = getItemsBegin(component);
    const std::size_t size = getItemsSize(component);
    if (!begin) return nullptr;

    for (std::size_t i = 0; i < size; ++i) {
        void* item = begin[i];
        if (!item) continue;

        const auto address = reinterpret_cast<std::uintptr_t>(item);
        const auto type = *reinterpret_cast<const std::uint8_t*>(
            address + bedrocktools::sdk::offsets::DataItem::mType
        );
        if (type == 0) return *reinterpret_cast<void**>(item);
    }

    return nullptr;
}

bool readAlwaysShowItem(void* actor, std::int8_t& value) {
    void* component = getDataComponent(actor);
    if (!component) return false;

    constexpr std::size_t id = bedrocktools::sdk::offsets::ActorDataIds::NametagAlwaysShow;
    if (getItemsSize(component) <= id) return false;

    auto** begin = getItemsBegin(component);
    if (!begin) return false;

    void* item = begin[id];
    if (!item) return false;

    const auto address = reinterpret_cast<std::uintptr_t>(item);
    const auto type = *reinterpret_cast<const std::uint8_t*>(
        address + bedrocktools::sdk::offsets::DataItem::mType
    );
    const auto itemId = *reinterpret_cast<const std::uint16_t*>(
        address + bedrocktools::sdk::offsets::DataItem::mId
    );
    if (type != 0 || itemId != id) return false;

    value = *reinterpret_cast<const std::int8_t*>(
        address + bedrocktools::sdk::offsets::DataItem::mValue
    );
    return true;
}

bool writeAlwaysShowItem(void* actor, std::int8_t value) {
    if (!actor || !g_ensureIndex || !g_updateAlwaysShowNameTag) return false;

    void* wrapper = getEntityDataWrapper(actor);
    void* context = getEntityContext(actor);
    void* component = getDataComponent(actor);
    if (!wrapper || !context || !component) return false;

    constexpr std::uint16_t id = static_cast<std::uint16_t>(
        bedrocktools::sdk::offsets::ActorDataIds::NametagAlwaysShow
    );

    g_ensureIndex(component, id);

    auto** begin = getItemsBegin(component);
    if (!begin || getItemsSize(component) <= id) return false;

    void* item = begin[id];
    if (!item) {
        void* vtable = findSCharDataItemVtable(component);
        if (!vtable) return false;

        item = ::operator new(16);
        std::memset(item, 0, 16);
        *reinterpret_cast<void**>(item) = vtable;
        *reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(item) + bedrocktools::sdk::offsets::DataItem::mType
        ) = 0;
        *reinterpret_cast<std::uint16_t*>(
            reinterpret_cast<std::uintptr_t>(item) + bedrocktools::sdk::offsets::DataItem::mId
        ) = id;
        begin[id] = item;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(item);
    const auto type = *reinterpret_cast<const std::uint8_t*>(
        address + bedrocktools::sdk::offsets::DataItem::mType
    );
    const auto itemId = *reinterpret_cast<const std::uint16_t*>(
        address + bedrocktools::sdk::offsets::DataItem::mId
    );
    if (type != 0 || itemId != id) return false;

    *reinterpret_cast<std::int8_t*>(
        address + bedrocktools::sdk::offsets::DataItem::mValue
    ) = value;

    markDataItemPresentAndDirty(component, id);
    g_updateAlwaysShowNameTag(context, wrapper);
    return true;
}

int readFuseTicks(void* actor) {
    if (!actor) return -1;

    void* component = getDataComponent(actor);
    if (!component) return -1;

    constexpr std::size_t fuseId = bedrocktools::sdk::offsets::ActorDataIds::FuseTime;
    if (getItemsSize(component) <= fuseId) return -1;

    auto** begin = getItemsBegin(component);
    if (!begin) return -1;

    void* item = begin[fuseId];
    if (!item) return -1;

    const auto itemAddress = reinterpret_cast<std::uintptr_t>(item);
    const auto type = *reinterpret_cast<const std::uint8_t*>(
        itemAddress + bedrocktools::sdk::offsets::DataItem::mType
    );
    const auto id = *reinterpret_cast<const std::uint16_t*>(
        itemAddress + bedrocktools::sdk::offsets::DataItem::mId
    );
    if (type != bedrocktools::sdk::offsets::DataItem::IntType || id != fuseId) return -1;

    return *reinterpret_cast<const int*>(
        itemAddress + bedrocktools::sdk::offsets::DataItem::mValue
    );
}

void restoreNametag(void* actor) {
    const auto it = g_originalStates.find(actor);
    if (it == g_originalStates.end()) return;

    if (g_setNameTag) g_setNameTag(actor, it->second.name);
    writeAlwaysShowItem(actor, it->second.hadAlwaysShowItem ? it->second.alwaysShowValue : 0);
    g_originalStates.erase(it);
}

void captureNametag(void* actor) {
    if (!actor || g_originalStates.contains(actor)) return;

    OriginalNametagState state;
    if (g_getNameTag) state.name = g_getNameTag(actor);
    state.hadAlwaysShowItem = readAlwaysShowItem(actor, state.alwaysShowValue);
    g_originalStates.emplace(actor, std::move(state));
}

std::string formatTimer(int fuseTicks) {
    float seconds = static_cast<float>(fuseTicks + 1) / 20.0f;
    seconds = std::max(seconds, 0.0f);

    const char* color = "\xC2\xA7" "c";
    if (seconds >= 3.0f) color = "\xC2\xA7" "a";
    else if (seconds >= 2.0f) color = "\xC2\xA7" "e";
    else if (seconds >= 1.0f) color = "\xC2\xA7" "6";

    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%s%.2fs", color, seconds);
    return buffer;
}

void primedTntNormalTickHook(void* actor) {
    if (g_normalTickOriginal) g_normalTickOriginal(actor);
    if (!actor || !g_tntTimer) return;

    const int fuseTicks = readFuseTicks(actor);
    if (fuseTicks < 0) {
        g_originalStates.erase(actor);
        return;
    }

    if (!g_tntTimer->enabled) {
        restoreNametag(actor);
        return;
    }

    if (fuseTicks <= 0) {
        g_originalStates.erase(actor);
        return;
    }

    if (!g_setNameTag || !g_ensureIndex || !g_updateAlwaysShowNameTag) return;

    captureNametag(actor);
    g_setNameTag(actor, formatTimer(fuseTicks));
    writeAlwaysShowItem(actor, 1);
}
}

TntTimerModule::TntTimerModule()
    : Module("TNT Timer", "Shows a live fuse countdown above every primed TNT without requiring hover") {
    g_tntTimer = this;
}

TntTimerModule::~TntTimerModule() {
    if (g_tntTimer == this) g_tntTimer = nullptr;
}

void TntTimerModule::onInit() {
    g_getNameTag = reinterpret_cast<ActorGetNameTagFn>(
        bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorGetNameTag)
    );
    g_setNameTag = reinterpret_cast<ActorSetNameTagFn>(
        bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::ActorSetNameTag)
    );
    g_ensureIndex = reinterpret_cast<SynchedActorDataEnsureIndexFn>(
        bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::SynchedActorDataEnsureIndex)
    );
    g_updateAlwaysShowNameTag = reinterpret_cast<ActorSynchedDataUpdateAlwaysShowNameTagFn>(
        bedrocktools::memory::resolve(
            bedrocktools::memory::SignatureId::ActorSynchedDataUpdateAlwaysShowNameTag
        )
    );

    const auto normalTick = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::PrimedTntNormalTick
    );
    if (!normalTick || g_normalTickOriginal) return;

    bedrocktools::hooks::install(
        reinterpret_cast<void*>(normalTick),
        reinterpret_cast<void*>(primedTntNormalTickHook),
        reinterpret_cast<void**>(&g_normalTickOriginal)
    );
}
