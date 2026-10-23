#pragma once
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/Types.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "sm_status.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tess_util {

using TessBegin = void (*)(void*, void*, int, int, int);
using TessColor = void (*)(void*, float, float, float, float);
using TessVertex = void (*)(void*, float, float, float);
using RenderMesh = void (*)(void*, void*, void*, char*);
using Line = std::pair<bedrocktools::sdk::Vec3, bedrocktools::sdk::Vec3>;
using Lines = std::vector<Line>;

inline constexpr uint8_t kVertexPat[] = {
    0x05,0x10,0x2E,0x1E, 0xE6,0x03,0x22,0x1E, 0x06,0x10,0x2E,0x1E, 0x70,0x20,0x25,0x1E,
    0x4D,0x01,0x00,0x54, 0x90,0x20,0x25,0x1E, 0xAD,0x01,0x00,0x54, 0x08,0x50,0x46,0x39
};

inline uintptr_t scanNear(uintptr_t anchor, size_t before, size_t after) {
    if (!anchor) return 0;
    uintptr_t start = anchor > before ? anchor - before : anchor;
    uintptr_t end = anchor + after;
    const size_t n = sizeof(kVertexPat);
    for (uintptr_t p = start; p + n < end; p += 4) {
        bool ok = true;
        for (size_t i = 0; i < n; ++i) {
            if (*reinterpret_cast<const uint8_t*>(p + i) != kVertexPat[i]) {
                ok = false;
                break;
            }
        }
        if (ok) return p;
    }
    return 0;
}

struct Context {
    TessBegin begin = nullptr;
    TessColor color = nullptr;
    TessVertex vertex = nullptr;
    RenderMesh mesh = nullptr;
    bool readyFlag = false;
    bool drawDisabled = false; // set true after a detected bad draw
    int resolveAttempts = 0;
    int drawCount = 0;
    int crashGuard = 0; // increment through draw stages

    void resolve() {
        if (readyFlag) return;
        ++resolveAttempts;

        auto tb = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorBegin);
        auto tc = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorColor);
        auto tv = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorVertex);
        // Prefer Immediately (1) then Immediately2 — order used by chunkborder
        auto rm = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately);
        if (!rm) rm = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately2);

        if (tb) begin = (TessBegin)tb;
        if (tc) color = (TessColor)tc;
        if (tv) vertex = (TessVertex)tv;
        if (rm) mesh = (RenderMesh)rm;

        if (!vertex && color) {
            uintptr_t found = scanNear((uintptr_t)color, 0x1000, 0x4000);
            if (!found && begin) found = scanNear((uintptr_t)begin, 0x1000, 0x5000);
            if (found) {
                vertex = (TessVertex)found;
                sm_status::write("tess: vertex FOUND via scan @%p", (void*)found);
            }
        }

        if (begin && color && vertex && mesh) {
            readyFlag = true;
            sm_status::write("tess: READY begin=%p color=%p vertex=%p mesh=%p attempts=%d",
                (void*)begin, (void*)color, (void*)vertex, (void*)mesh, resolveAttempts);
        } else if (resolveAttempts == 1 || resolveAttempts % 600 == 0) {
            sm_status::write("tess: NOT ready attempt=%d begin=%p color=%p vertex=%p mesh=%p",
                resolveAttempts, (void*)begin, (void*)color, (void*)vertex, (void*)mesh);
        }
    }

    bool ready() {
        resolve();
        return readyFlag && !drawDisabled;
    }

    /** Draw lines relative to camera. Uses LRP selection material only (no getMaterial). */
    void drawLines(void* tess, void* sc, void* mat, float camX, float camY, float camZ,
                   const Lines& lines, uint32_t rgba) {
        if (!readyFlag || drawDisabled || lines.empty()) return;
        if (!tess || !sc || !mat) return;
        if ((uintptr_t)tess < 0x1000 || (uintptr_t)sc < 0x1000 || (uintptr_t)mat < 0x1000) return;

        float r = ((rgba >> 16) & 0xFF) / 255.f;
        float g = ((rgba >>  8) & 0xFF) / 255.f;
        float b = ((rgba      ) & 0xFF) / 255.f;
        float a = ((rgba >> 24) & 0xFF) / 255.f;
        if (a < 0.05f) a = 1.f;

        // Cap vertices — huge batches can overflow tess buffer
        size_t maxLines = lines.size();
        if (maxLines > 256) maxLines = 256;

        crashGuard = 1;
        begin(tess, nullptr, 4, (int)maxLines * 2, 0);
        crashGuard = 2;
        color(tess, r, g, b, a);
        crashGuard = 3;
        for (size_t i = 0; i < maxLines; ++i) {
            auto p1 = lines[i].first, p2 = lines[i].second;
            p1.x -= camX; p1.y -= camY; p1.z -= camZ;
            p2.x -= camX; p2.y -= camY; p2.z -= camZ;
            vertex(tess, p1.x, p1.y, p1.z);
            vertex(tess, p2.x, p2.y, p2.z);
        }
        crashGuard = 4;
        // chunkborder uses 0x58 zeroed pad
        alignas(16) char pad[0x58];
        std::memset(pad, 0, sizeof(pad));
        mesh(sc, tess, mat, pad);
        crashGuard = 5;
        ++drawCount;
    }

    static void addBox(Lines& out, float minx, float miny, float minz,
                       float maxx, float maxy, float maxz) {
        bedrocktools::sdk::Vec3 p[8] = {
            {minx,miny,minz},{maxx,miny,minz},{maxx,miny,maxz},{minx,miny,maxz},
            {minx,maxy,minz},{maxx,maxy,minz},{maxx,maxy,maxz},{minx,maxy,maxz}};
        int e[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for (auto& ed : e) out.push_back({p[ed[0]], p[ed[1]]});
    }

    static void addRing(Lines& out, float cx, float cz, float y, float rx, float rz, int segs, float spin) {
        if (segs > 48) segs = 48;
        for (int i = 0; i < segs; ++i) {
            float a0 = (float)(2.0 * M_PI * i / segs) + spin;
            float a1 = (float)(2.0 * M_PI * (i + 1) / segs) + spin;
            out.push_back({{cx + std::cos(a0)*rx, y, cz + std::sin(a0)*rz},
                           {cx + std::cos(a1)*rx, y, cz + std::sin(a1)*rz}});
        }
    }
};

inline Context& ctx() {
    static Context c;
    return c;
}

} // namespace tess_util
