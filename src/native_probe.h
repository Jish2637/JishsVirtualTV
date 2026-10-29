#pragma once
#include <string>
namespace virtualtv {
struct NativeCompatibility {
    bool skate_present{};
    bool picking_verified{};
    bool movement_verified{};
    std::wstring details;
};
NativeCompatibility probe_native_compatibility() noexcept;
}
