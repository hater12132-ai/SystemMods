#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>

namespace sm_status {

inline void write(const char* fmt, ...) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    // Levi media folder (same place as bactro_status)
    const char* paths[] = {
        "/sdcard/Android/media/org.levimc.launcher/systemmods_status.txt",
        "/storage/emulated/0/Android/media/org.levimc.launcher/systemmods_status.txt",
        "/sdcard/Download/systemmods_status.txt",
    };
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (const char* p : paths) {
        FILE* f = fopen(p, "a");
        if (!f) continue;
        fprintf(f, "%s\n", buf);
        fclose(f);
        break;
    }
}

inline void clear() {
    const char* paths[] = {
        "/sdcard/Android/media/org.levimc.launcher/systemmods_status.txt",
        "/storage/emulated/0/Android/media/org.levimc.launcher/systemmods_status.txt",
        "/sdcard/Download/systemmods_status.txt",
    };
    for (const char* p : paths) {
        FILE* f = fopen(p, "w");
        if (!f) continue;
        fprintf(f, "SystemMods status log\n");
        fclose(f);
        break;
    }
}

} // namespace sm_status
