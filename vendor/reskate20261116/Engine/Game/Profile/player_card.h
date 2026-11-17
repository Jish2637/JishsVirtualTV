#pragma once
#include <string>

namespace dingosdk {
// Local RIP Card presentation only. Multiplayer identities always use Steam.
struct PlayerCardModel {
    bool available{};
    std::string custom_name; // Empty: the card shows the Steam name.
    std::string feedback;
};
}
