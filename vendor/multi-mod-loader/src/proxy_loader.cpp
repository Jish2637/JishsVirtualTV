// Universal dinput8.dll proxy for ReSkate mods.
//
// Forwards DirectInput (to dinput8_chain.dll beside Skate.exe when present, else System32) and, once
// ReSkate reports its overlay ready, loads every mod DLL listed in scripts/Custom/dinput8_mods.txt
// (one file name per line, relative to scripts/Custom; "# name.dll" switches a mod off). The list is
// synced with the folder at every launch: new DLLs are appended, deleted ones removed (mod_list.h), so
// existing single-mod installs (for example JishMSCBroadcast.dll) keep loading. Each mod still applies
// its own launcher on/off switch. Based on the single-DLL proxy in Jish's First Person Mod / JishMSCBroadcast.
#include <Windows.h>
#define DIRECTINPUT_VERSION 0x800
#include <dinput.h>
#include "Engine/Game/Build/image_identity.h"
#include "rotating_log.h"
#include "loader/mod_list.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
HMODULE self{};
INIT_ONCE forwarding_once = INIT_ONCE_STATIC_INIT;
HMODULE forward{};
struct OverlayStatus {
    bool started{}, bound{}, ready{}, visible{}, stopping{}, failed{};
    std::uint64_t rendered_frames{};
};
std::filesystem::path self_directory() {
    wchar_t path[32768]{};
    const auto size = GetModuleFileNameW(self, path, 32768);
    if (!size || size >= 32768) return {};
    return std::filesystem::path(path).parent_path();
}
void log(const std::string& message) noexcept {
    try {
        const auto root = self_directory();
        if (root.empty()) return;
        jishsfirstpersonmod::append_rotating_log(root / L"scripts/Custom/UniversalLoader/loader.log",
            std::to_string(GetTickCount64()) + " [dinput8 universal loader] " + message + '\n', jishsfirstpersonmod::text_log_limit);
    } catch (...) {}
}
BOOL CALLBACK initialize_forwarding(PINIT_ONCE, PVOID, PVOID*) {
    const auto chain = self_directory() / L"dinput8_chain.dll";
    if (!self_directory().empty() && std::filesystem::is_regular_file(chain)) {
        forward = LoadLibraryExW(chain.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (forward == self) forward = nullptr;
        if (forward && GetProcAddress(forward, "DirectInput8Create")) return TRUE;
        forward = nullptr;
    }
    wchar_t system[MAX_PATH]{};
    const auto size = GetSystemDirectoryW(system, MAX_PATH);
    if (!size || size >= MAX_PATH) return FALSE;
    forward = LoadLibraryExW((std::filesystem::path(system) / L"dinput8.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return forward != nullptr;
}
template<class T> T native(const char* name) {
    if (!InitOnceExecuteOnce(&forwarding_once, initialize_forwarding, nullptr, nullptr)) return nullptr;
    return reinterpret_cast<T>(GetProcAddress(forward, name));
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
// Syncs dinput8_mods.txt with the DLLs in scripts/Custom (see mod_list.h) and returns the mods to load.
std::vector<std::wstring> mod_list(const std::filesystem::path& custom) {
    const auto list = custom / L"dinput8_mods.txt";
    std::vector<std::wstring> present;
    for (const auto& entry : std::filesystem::directory_iterator(custom))
        if (entry.is_regular_file() && !universal_loader::dll_name(entry.path().filename().wstring()).empty())
            present.push_back(entry.path().filename().wstring());
    const bool exists = std::filesystem::exists(list);
    std::vector<std::wstring> lines;
    if (exists) {
        std::ifstream in(list, std::ios::binary);
        for (std::string line; std::getline(in, line);) {
            if (line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(wide(line));
        }
    }
    auto result = universal_loader::sync(lines, exists, present);
    for (const auto& name : result.added) log("Added new mod to the list: " + utf8(name));
    for (const auto& name : result.removed) log("Removed from the list (DLL no longer in scripts/Custom, or listed twice): " + utf8(name));
    if (!exists || result.changed()) {
        // Write beside the list, then replace it, so a crash never leaves a half-written list.
        const auto temporary = custom / L"dinput8_mods.txt.tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            for (const auto& line : result.lines) out << utf8(line) << "\r\n";
            if (!out) throw std::runtime_error("cannot write dinput8_mods.txt.tmp");
        }
        if (!MoveFileExW(temporary.c_str(), list.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            log("Could not update dinput8_mods.txt (Win32 " + std::to_string(GetLastError()) + "); loading the synced list anyway.");
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        } else log(std::string(exists ? "Updated" : "Created") + " scripts/Custom/dinput8_mods.txt.");
    }
    return result.load;
}
DWORD WINAPI load_mods(void*) noexcept {
    try {
        wchar_t host[32768]{};
        if (!GetModuleFileNameW(nullptr, host, 32768)) return 0;
        if (_wcsicmp(std::filesystem::path(host).filename().c_str(), L"Skate.exe") != 0) return 0;
        (void)native<void*>("DirectInput8Create"); // bring up a chained proxy early
        if (!dingosdk::supported_build::running_image_matches(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))) {
            log("Unsupported Skate.exe; no mods loaded. DirectInput forwarding remains active.");
            return 0;
        }
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&load_mods), &pinned)) return 0;
        const auto custom = self_directory() / L"scripts/Custom";
        if (!std::filesystem::is_directory(custom)) { log("scripts/Custom missing; no mods loaded."); return 0; }
        const auto mods = mod_list(custom);
        if (mods.empty()) { log("No mods listed; DirectInput forwarding only."); return 0; }
        log("Loaded by Skate. Waiting for ReSkate graphics initialization before loading " + std::to_string(mods.size()) + " mod(s).");
        using Status = void (*)(OverlayStatus*);
        bool ready = false;
        for (unsigned attempt = 0; attempt < 6000; ++attempt) {
            if (const auto runtime = GetModuleHandleW(L"ReSkate.dll")) {
                if (const auto query = reinterpret_cast<Status>(GetProcAddress(runtime, "DingoSDKOverlayGetStatus"))) {
                    OverlayStatus status{};
                    query(&status);
                    if (status.started && status.ready && !status.failed && !status.stopping) { ready = true; break; }
                    if (status.failed || status.stopping) { log("ReSkate failed or stopped; no mods loaded."); return 0; }
                }
            }
            Sleep(100);
        }
        if (!ready) { log("ReSkate readiness timed out; no mods loaded."); return 0; }
        for (const auto& name : mods) {
            const auto dll = custom / name;
            const auto label = utf8(name);
            if (!std::filesystem::is_regular_file(dll)) { log(label + ": missing; skipped."); continue; }
            if (GetModuleHandleW(name.c_str())) { log(label + ": already loaded by something else; skipped."); continue; }
            if (!LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
                log(label + ": LoadLibraryEx failed (Win32 " + std::to_string(GetLastError()) + ").");
                continue;
            }
            log(label + ": loaded.");
        }
    } catch (...) { log("Loader exception; DirectInput forwarding remains active."); }
    return 0;
}
} // namespace

extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* output, LPUNKNOWN outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto fn = native<Fn>("DirectInput8Create");
    return fn ? fn(instance, version, iid, output, outer) : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {
    const auto fn = native<HRESULT(WINAPI*)()>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    const auto fn = native<Fn>("DllGetClassObject");
    return fn ? fn(clsid, iid, output) : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    const auto fn = native<HRESULT(WINAPI*)()>("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    const auto fn = native<HRESULT(WINAPI*)()>("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}
extern "C" LPCDIDATAFORMAT WINAPI ProxyGetdfDIJoystick() {
    const auto fn = native<LPCDIDATAFORMAT(WINAPI*)()>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        if (const auto thread = CreateThread(nullptr, 0, load_mods, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
