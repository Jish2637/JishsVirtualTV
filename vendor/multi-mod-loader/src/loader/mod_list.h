#pragma once
// dinput8_mods.txt maintenance for the universal loader, kept free of Windows calls so it can be tested.
//
// The list holds one DLL file name per line, loaded top to bottom. "# name.dll" is a mod switched off by
// hand; any other '#' line is a comment. On every launch the list is synced with scripts/Custom:
// entries whose DLL is gone are removed (switched-off ones too), DLLs not yet listed are appended at the
// bottom, and everything else (order, comments, switched-off lines) stays as the user left it.
#include <algorithm>
#include <cwctype>
#include <set>
#include <string>
#include <vector>

namespace universal_loader {
inline std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}
inline std::wstring trim(const std::wstring& text) {
    const auto b = text.find_first_not_of(L" \t\r\n\xFEFF"), e = text.find_last_not_of(L" \t\r\n");
    return b == std::wstring::npos ? std::wstring{} : text.substr(b, e - b + 1);
}
// A plain file name ending in .dll (no folders), or empty.
inline std::wstring dll_name(const std::wstring& text) {
    const auto name = trim(text);
    if (name.size() < 5 || name.find_first_of(L"/\\:") != std::wstring::npos) return {};
    return lower(name.substr(name.size() - 4)) == L".dll" ? name : std::wstring{};
}
// The loader itself (or a chained proxy) must never be loaded as a mod.
inline bool loadable_mod(const std::wstring& name) { return !lower(name).starts_with(L"dinput8"); }

struct Entry { std::wstring name; bool enabled{}; };
inline bool parse_entry(const std::wstring& line, Entry& out) {
    auto text = trim(line);
    bool enabled = true;
    if (!text.empty() && text[0] == L'#') { enabled = false; text = trim(text.substr(1)); }
    const auto name = dll_name(text);
    if (name.empty()) return false;
    out = {name, enabled};
    return true;
}

struct SyncResult {
    std::vector<std::wstring> lines;   // the list file's new contents
    std::vector<std::wstring> load;    // enabled mods in order
    std::vector<std::wstring> added, removed;
    bool changed() const { return !added.empty() || !removed.empty(); }
};
inline const std::vector<std::wstring>& header_lines() {
    static const std::vector<std::wstring> lines{
        L"# Mods loaded by the universal dinput8.dll, top to bottom, from scripts/Custom.",
        L"# Synced at every game launch: new DLLs are added at the bottom, deleted ones are removed.",
        L"# Put '#' in front of a mod's line to switch it off (it stays listed); reorder lines to change load order."};
    return lines;
}
// present: DLL file names currently in scripts/Custom (sorted for a stable order of new additions).
inline SyncResult sync(const std::vector<std::wstring>& existing, bool list_exists, std::vector<std::wstring> present) {
    SyncResult r;
    std::erase_if(present, [](const auto& name) { return dll_name(name).empty(); });
    std::sort(present.begin(), present.end(), [](const auto& a, const auto& b) { return lower(a) < lower(b); });
    std::set<std::wstring> on_disk;
    for (const auto& p : present) on_disk.insert(lower(p));
    std::set<std::wstring> listed;
    if (!list_exists) r.lines = header_lines();
    for (const auto& line : existing) {
        Entry e;
        if (!parse_entry(line, e)) { r.lines.push_back(line); continue; } // comment or blank: keep
        const auto key = lower(e.name);
        if (!on_disk.contains(key) || listed.contains(key)) { r.removed.push_back(e.name); continue; }
        listed.insert(key);
        r.lines.push_back(line);
        if (e.enabled && loadable_mod(e.name)) r.load.push_back(e.name);
    }
    for (const auto& name : present) {
        const auto key = lower(name);
        if (listed.contains(key) || !loadable_mod(name)) continue;
        listed.insert(key);
        r.lines.push_back(name);
        r.load.push_back(name);
        r.added.push_back(name);
    }
    return r;
}
} // namespace universal_loader
