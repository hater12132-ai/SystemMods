#include "chattimestamps.hpp"

#include "core/memory/Hooks.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/offsets/UI.hpp>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace {

using GuiDataAddMessageFn = void (*)(void*, void*, std::int32_t);
using DisplayDevConsoleMessageFn = void (*)(void*, const std::string&);

ChatTimestampsModule* g_chatTimestamps = nullptr;
GuiDataAddMessageFn g_addMessageOriginal = nullptr;
DisplayDevConsoleMessageFn g_displayDevConsoleMessageOriginal = nullptr;

std::string makeTimestampPrefix() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    int hour = local.tm_hour;
    const char* period = hour >= 12 ? "PM" : "AM";
    if (hour == 0) hour = 12;
    else if (hour > 12) hour -= 12;

    char buffer[40]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "\xC2\xA7" "7[%d:%02d %s]" "\xC2\xA7" "r ",
        hour,
        local.tm_min,
        period
    );
    return buffer;
}

bool hasTimestampPrefix(std::string_view value) {
    constexpr std::string_view start = "\xC2\xA7" "7[";
    constexpr std::string_view end = "]" "\xC2\xA7" "r ";
    if (!value.starts_with(start)) return false;
    const auto endPosition = value.find(end, start.size());
    return endPosition != std::string_view::npos && endPosition <= 16;
}

void prependTimestamp(std::string& value, const std::string& prefix) {
    if (value.empty() || hasTimestampPrefix(value)) return;
    value.insert(0, prefix);
}

bool isDisplayMessageType(std::int32_t type) {
    return type >= 1 && type <= 8;
}

void addMessageHook(void* guiData, void* guiMessage, std::int32_t profanityFilterContext) {
    if (g_chatTimestamps && g_chatTimestamps->enabled && guiMessage) {
        auto* base = static_cast<std::uint8_t*>(guiMessage);
        const auto type = *reinterpret_cast<const std::int32_t*>(
            base + bedrocktools::sdk::offsets::GuiMessage::Type
        );

        if (isDisplayMessageType(type)) {
            const std::string prefix = makeTimestampPrefix();
            auto& fullString = *reinterpret_cast<std::string*>(
                base + bedrocktools::sdk::offsets::GuiMessage::FullString
            );
            prependTimestamp(fullString, prefix);

            const bool hasFilteredFullString = *reinterpret_cast<const std::uint8_t*>(
                base + bedrocktools::sdk::offsets::GuiMessage::FilteredFullStringPresent
            ) != 0;
            if (hasFilteredFullString) {
                auto& filteredFullString = *reinterpret_cast<std::string*>(
                    base + bedrocktools::sdk::offsets::GuiMessage::FilteredFullString
                );
                prependTimestamp(filteredFullString, prefix);
            }
        }
    }

    if (g_addMessageOriginal) {
        g_addMessageOriginal(guiData, guiMessage, profanityFilterContext);
    }
}

std::string timestampDevConsoleLines(const std::string& messages) {
    const std::string prefix = makeTimestampPrefix();
    if (messages.empty()) return prefix;

    std::string result;
    result.reserve(messages.size() + prefix.size() * 2);

    std::size_t lineStart = 0;
    while (lineStart < messages.size()) {
        const std::size_t newline = messages.find('\n', lineStart);
        const std::string_view line(
            messages.data() + lineStart,
            newline == std::string::npos ? messages.size() - lineStart : newline - lineStart
        );
        if (!hasTimestampPrefix(line)) result += prefix;
        result.append(line.data(), line.size());

        if (newline == std::string::npos) break;
        result.push_back('\n');
        lineStart = newline + 1;
    }

    return result;
}

void displayDevConsoleMessageHook(void* guiData, const std::string& messages) {
    if (!g_displayDevConsoleMessageOriginal) return;
    if (!g_chatTimestamps || !g_chatTimestamps->enabled) {
        g_displayDevConsoleMessageOriginal(guiData, messages);
        return;
    }

    const std::string timestamped = timestampDevConsoleLines(messages);
    g_displayDevConsoleMessageOriginal(guiData, timestamped);
}

}

ChatTimestampsModule::ChatTimestampsModule()
    : Module(
        "Chat Timestamps",
        "Adds local timestamps to chat, client, localized, system, whisper, text-object, announcement, and developer-console messages."
    ) {
    g_chatTimestamps = this;
}

ChatTimestampsModule::~ChatTimestampsModule() {
    if (g_chatTimestamps == this) g_chatTimestamps = nullptr;
}

void ChatTimestampsModule::onInit() {
    const auto addMessage = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::GuiDataAddMessage
    );
    if (addMessage && !g_addMessageOriginal) {
        bedrocktools::hooks::install(
            reinterpret_cast<void*>(addMessage),
            reinterpret_cast<void*>(addMessageHook),
            reinterpret_cast<void**>(&g_addMessageOriginal)
        );
    }

    const auto devConsole = bedrocktools::memory::resolve(
        bedrocktools::memory::SignatureId::GuiDataDisplayDevConsoleMessage
    );
    if (devConsole && !g_displayDevConsoleMessageOriginal) {
        bedrocktools::hooks::install(
            reinterpret_cast<void*>(devConsole),
            reinterpret_cast<void*>(displayDevConsoleMessageHook),
            reinterpret_cast<void**>(&g_displayDevConsoleMessageOriginal)
        );
    }
}
