#include "native_probe.h"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
namespace virtualtv {
namespace {
// Byte prefixes from Dingo-Shenanigans/ReSkate, v20260929/park_editor.h.
// This probe never calls or patches the functions.
struct Signature { std::uintptr_t rva; std::array<unsigned char, 16> prefix; };
constexpr Signature picking[] = {
    {0x25d5430, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x10,0x48,0x8b,0xda,0x44,0x8b,0x49}},
    {0x25d7e00, {0x4c,0x8b,0xc1,0x8b,0x49,0x08,0x83,0xf9,0xff,0x74,0x3a,0x49,0x8b,0x00,0x8b,0xd1}},
    {0x46aa70,  {0x40,0x53,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x48,0x8b,0x05,0x40,0x99,0xd5,0x06}}
};
constexpr Signature movement[] = {
    {0x6fd220, {0x40,0x53,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x48,0x8b,0x05,0x90,0x71,0xac,0x06}},
    {0x25dcbf0, {0x48,0x83,0xec,0x48,0xc5,0xf8,0x10,0x42,0x10,0xc5,0xf8,0x10,0x0a,0x48,0x8d,0x54}},
    {0x287c980, {0x48,0x8b,0xc4,0x53,0x57,0x41,0x55,0x41,0x56,0x48,0x81,0xec,0x18,0x01,0x00,0x00}}
};
bool match(HMODULE base, const Signature& signature) noexcept {
    if(!base) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(base) + signature.rva;
    unsigned char bytes[16]{};
    SIZE_T got{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),bytes,sizeof bytes,&got) &&
        got==sizeof bytes && std::memcmp(bytes,signature.prefix.data(),sizeof bytes)==0;
}
template<std::size_t N> bool matches(HMODULE base,const Signature (&items)[N]) noexcept {
    for(const auto& s:items) if(!match(base,s)) return false;
    return true;
}
}
NativeCompatibility probe_native_compatibility() noexcept {
    NativeCompatibility out;
    const HMODULE skate=GetModuleHandleW(L"Skate.exe");
    out.skate_present = skate != nullptr;
    if (skate) {
        out.picking_verified = matches(skate,picking);
        out.movement_verified = matches(skate,movement);
    }
    std::wostringstream stream;
    stream << L"Skate.exe: " << (out.skate_present?L"found":L"missing")
           << L" | object picking: " << (out.picking_verified?L"verified":L"unverified")
           << L" | object movement: " << (out.movement_verified?L"verified":L"unverified");
    out.details=stream.str();
    return out;
}
}
