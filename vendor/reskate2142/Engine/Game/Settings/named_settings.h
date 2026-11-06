#pragma once
#include <string>
#include <vector>
namespace dingosdk {
struct NamedSettingModel {
    std::string name, type, value, reason;
    bool available = false, override_active = false;
    std::vector<std::string> choices;
};
} // namespace dingosdk
