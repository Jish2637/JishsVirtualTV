#include "client_source_spawn.h"
#include "client_source_spawn_internal.h"

namespace dingosdk::client_source::detail {
SourceState& source_state() { static SourceState state; return state; }
bool source_writable(std::uintptr_t address, std::size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!source_range(address, size) || !VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info))) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto protection = info.Protect & 0xff;
    return info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD) && address >= begin &&
        address - begin <= info.RegionSize && size <= info.RegionSize - (address - begin) &&
        (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
         protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY);
}
}

namespace dingosdk {
using namespace client_source::detail;

void initialize_client_source_spawn(std::uintptr_t base) {
    SourceLastError error;
    auto& state = source_state();
    std::lock_guard lock(state.initialization_mutex);
    if (state.initialized.load()) return;
    auto& trial = state.trial;
    trial.camera_mode = reinterpret_cast<SourceCameraMode>(base + spawn::camera_mode_switch);
    trial.camera_transform = reinterpret_cast<SourceCameraTransform>(base + spawn::camera_transform_setter);
    trial.base = base;
    state.initialized.store(true, std::memory_order_release);
}
}

