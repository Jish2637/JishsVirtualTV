#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace dingosdk {
struct ObjectPersistenceRow {
    std::uint64_t token{}; // Session-scoped UI identity, never a native entity pointer.
    std::string item;
    std::array<float, 3> position{};
    bool saved{}, spawned{};
};
struct ObjectPersistenceModel {
    bool available{}, enabled{true}, can_clear{}, clearing{};
    std::size_t count{};
    std::string map, status;
    std::vector<ObjectPersistenceRow> rows;
    bool busy{};
};
}
