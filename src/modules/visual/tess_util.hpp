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

struct HashedString {
    uint64_t mStrHash{};
    std::string mStr;
    mutable const HashedString* mLastMatch{nullptr};
    HashedString() = default;
    explicit HashedString(const char* str) {
        mStr = str ? str : "";
        constexpr uint64_t kO = 0xCBF29CE484222325ULL, kP = 0x100000001B3ULL;
        uint64_t h = kO;
        for (char c : mStr) h = (uint64_t)(unsigned char)c ^ (kP * h);
        mStrHash = mStr.empty() ? 0 : h;
    }
};

struct MaterialPtr {
    void* sharedPtrData[2]{nullptr, nullptr};
    MaterialPtr() = default;
    MaterialPtr(const MaterialPtr&) = delete;
    MaterialPtr& operator=(const MaterialPtr&) = delete;
    MaterialPtr(MaterialPtr&& o) noexcept {
        sharedPtrData[0] = o.sharedPtrData[0];
        sharedPtrData[1] = o.sharedPtrData[1];
        o.sharedPtrData[0] = o.sharedPtrData[1] = nullptr;
    }
    MaterialPtr& operator=(MaterialPtr&& o) noexcept {
        if (this != &o) {
            sharedPtrData[0] = o.sharedPtrData[0];
            sharedPtrData[1] = o.sharedPtrData[1];
            o.sharedPtrData[0] = o.sharedPtrData[1] = nullptr;
        }
        return *this;
    }
    explicit operator bool() const { return sharedPtrData[0] != nullptr; }
};

inline uintptr_t resolveADRP(uint32_t* insns, size_t count, uint32_t targetReg) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t insn = insns[i];
        if ((insn & 0x9F000000) != 0x90000000) continue;
        if ((insn & 0x1F) != targetReg) continue;
        int64_t immhi = (insn >> 5) & 0x7FFFF;
        int64_t immlo = (insn >> 29) & 0x3;
        int64_t imm = (immhi << 2) | immlo;
        imm = (imm << 43) >> 43;
        uintptr_t page = ((uintptr_t)&insns[i] & ~0xFFFULL) + (uintptr_t)(imm << 12);
        for (size_t j = i + 1; j < count && j < i + 8; ++j) {
            uint32_t add = insns[j];
            if ((add & 0xFF000000) == 0x91000000 &&
                ((add >> 5) & 0x1F) == targetReg && (add & 0x1F) == targetReg) {
                uint32_t imm12 = (add >> 10) & 0xFFF;
                if (add & 0x400000) imm12 <<= 12;
                return page + imm12;
            }
            if ((add & 0x1F) == targetReg) break;
        }
    }
    return 0;
}

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
    MaterialPtr matSelection;
    uintptr_t matGroup = 0;
    bool readyFlag = false;
    int resolveAttempts = 0;

    void resolve() {
        if (readyFlag) return;
        ++resolveAttempts;

        auto tb = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorBegin);
        auto tc = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorColor);
        auto tv = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::TessellatorVertex);
        auto rm = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately2);
        if (!rm) rm = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::MeshHelpersRenderMeshImmediately);

        if (tb) begin = (TessBegin)tb;
        if (tc) color = (TessColor)tc;
        if (tv) vertex = (TessVertex)tv;
        if (rm) mesh = (RenderMesh)rm;

        if (!vertex && color) {
            uintptr_t found = scanNear((uintptr_t)color, 0x1000, 0x4000);
            if (!found && begin)
                found = scanNear((uintptr_t)begin, 0x1000, 0x5000);
            if (found) {
                vertex = (TessVertex)found;
                sm_status::write("tess: vertex FOUND via scan @%p", (void*)found);
            }
        }

        if (!matGroup) {
            auto rmg = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderMaterialGroupCommon);
            if (rmg) {
                uintptr_t g = resolveADRP(reinterpret_cast<uint32_t*>(rmg), 8, 0);
                if (!g) g = resolveADRP(reinterpret_cast<uint32_t*>(rmg), 8, 1);
                if (g) matGroup = g + bedrocktools::sdk::offsets::MaterialGroup::mRenderMaterialGroupOffset;
            }
        }

        if (begin && color && vertex && mesh) {
            readyFlag = true;
            sm_status::write("tess: READY begin=%p color=%p vertex=%p mesh=%p attempts=%d",
                (void*)begin, (void*)color, (void*)vertex, (void*)mesh, resolveAttempts);
            return;
        }

        if (resolveAttempts == 1 || resolveAttempts == 30 || resolveAttempts % 600 == 0) {
            sm_status::write("tess: NOT ready attempt=%d begin=%p color=%p vertex=%p mesh=%p",
                resolveAttempts, (void*)begin, (void*)color, (void*)vertex, (void*)mesh);
        }
    }

    MaterialPtr getMaterial(const char* name) {
        if (!matGroup) return {};
        HashedString hs(name);
        void** vt = *reinterpret_cast<void***>(matGroup);
        if (!vt || !vt[2]) return {};
        using Fn = MaterialPtr(*)(void*, const HashedString*);
        return reinterpret_cast<Fn>(vt[2])((void*)matGroup, &hs);
    }

    void ensureMat() {
        if (!matSelection && matGroup)
            matSelection = getMaterial("selection_box");
    }

    bool ready() {
        resolve();
        return readyFlag;
    }

    void drawLines(void* tess, void* sc, void* mat, float camX, float camY, float camZ,
                   const Lines& lines, uint32_t rgba) {
        if (!readyFlag || lines.empty()) return;
        float r = ((rgba >> 16) & 0xFF) / 255.f;
        float g = ((rgba >>  8) & 0xFF) / 255.f;
        float b = ((rgba      ) & 0xFF) / 255.f;
        float a = ((rgba >> 24) & 0xFF) / 255.f;
        begin(tess, nullptr, 4, (int)lines.size() * 2, 0);
        color(tess, r, g, b, a);
        for (auto& ln : lines) {
            auto p1 = ln.first, p2 = ln.second;
            p1.x -= camX; p1.y -= camY; p1.z -= camZ;
            p2.x -= camX; p2.y -= camY; p2.z -= camZ;
            vertex(tess, p1.x, p1.y, p1.z);
            vertex(tess, p2.x, p2.y, p2.z);
        }
        char pad[64]{};
        mesh(sc, tess, mat, pad);
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
