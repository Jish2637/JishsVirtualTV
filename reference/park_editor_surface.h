#pragma once
#include "Engine/Core/Platform/memory.h"
#include "editor_math.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/park_editor.h"
#include "Engine/Game/World/park_editor.h"
#include <algorithm>
#include <optional>
#include <span>

namespace dingosdk::editor {
using SurfaceOwner = std::uint64_t (*)(std::uintptr_t, std::uint32_t, std::uintptr_t);
using SurfaceBody = std::array<std::uint64_t, 2>;
using SurfaceBodyReader = bool (*)(std::uintptr_t, std::uint32_t, std::uintptr_t, SurfaceBody &);
// addr::park_editor::surface_contracts[1] is the native camera ray query;
// the dropper's sphere query uses the same implementation and collision filter.
struct alignas(16) NativeSurfaceRay {
    std::array<float, 4> start{}, end{};
    std::uint32_t filter{0x40080303}, mode{};
    float tolerance{};
    std::uint32_t flags{};
    std::uintptr_t allocator_vtable{}, allocator{};
    std::array<std::uintptr_t, 5> ignored_bodies{};
    std::uint64_t padding{};
};
struct alignas(16) NativeSurfaceResult {
    std::uintptr_t world{}, data{}, allocator_vtable{}, allocator{};
    std::uint32_t first{}, count{};
    std::uint64_t padding{};
    std::array<std::uintptr_t, 4> scope{};
};
static_assert(offsetof(NativeSurfaceRay, ignored_bodies) == 0x40);
static_assert(offsetof(NativeSurfaceResult, scope) == 0x30 && sizeof(NativeSurfaceResult) == 0x50);
struct NativeSurfaceApi {
    std::uintptr_t (*world)(std::uintptr_t){};
    void *(*ray)(std::uintptr_t, NativeSurfaceResult *, const NativeSurfaceRay *, const char *){};
    std::uintptr_t (*scope_allocator)(){};
    void (*release)(void *, std::uintptr_t){};
    bool ready{};
};
inline NativeSurfaceApi surface_api(std::uintptr_t base) {
    const auto &surface_contracts = addr::park_editor::surface_contracts;
    NativeSurfaceApi api;
    api.ready = std::all_of(surface_contracts.begin(), surface_contracts.end(), [&](const auto &contract) {
        std::array<unsigned char, 32> bytes{};
        return memory::read_bytes(base + contract.rva, bytes.data(), bytes.size()) && bytes == contract.bytes;
    });
    if (api.ready) {
        api.world = reinterpret_cast<decltype(api.world)>(base + surface_contracts[0].rva);
        api.ray = reinterpret_cast<decltype(api.ray)>(base + surface_contracts[1].rva);
        api.scope_allocator =
            reinterpret_cast<decltype(api.scope_allocator)>(base + surface_contracts[2].rva);
        api.release = reinterpret_cast<decltype(api.release)>(base + surface_contracts[3].rva);
    }
    return api;
}
inline std::optional<Vec3> cast_surface_once(const NativeSurfaceApi &api, std::uintptr_t world, Vec3 start,
                                             Vec3 end, SurfaceOwner owner,
                                             std::span<const std::uint64_t> ignored, std::uint64_t *entity,
                                             std::span<const SurfaceBody> ignored_bodies,
                                             std::vector<SurfaceBody> &discovered,
                                             SurfaceBodyReader body_reader) {
    NativeSurfaceRay request;
    std::vector<SurfaceBody> bodies;
    for (const auto &body : ignored_bodies)
        if (body[0] == world)
            bodies.push_back(body);
    if (!bodies.empty()) {
        // The native ray implementation reads begin/end and each 16-byte body's index at +8.
        request.ignored_bodies[0] = reinterpret_cast<std::uintptr_t>(bodies.data());
        request.ignored_bodies[1] = reinterpret_cast<std::uintptr_t>(bodies.data() + bodies.size());
        request.ignored_bodies[2] = request.ignored_bodies[1];
    }
    std::copy(start.begin(), start.end(), request.start.begin());
    std::copy(end.begin(), end.end(), request.end.begin());
    NativeSurfaceResult result;
    api.ray(world, &result, &request, "ReSkate_ParkSurface");
    struct Release {
        const NativeSurfaceApi &api;
        NativeSurfaceResult &result;
        ~Release() {
            // Same result cleanup as the native ray callers. Pop the temporary
            // native allocation scope before releasing an explicit allocator.
            if (result.scope[2]) {
                const auto allocator = api.scope_allocator();
                std::uintptr_t vtable{}, function{};
                if (memory::read(allocator, vtable) && memory::read(vtable + 8, function) && function)
                    reinterpret_cast<void (*)(std::uintptr_t, void *)>(function)(allocator,
                                                                                 result.scope.data());
            }
            if (result.allocator)
                api.release(&result.allocator_vtable, result.data);
        }
    } release{api, result};
    if (result.world != world || !result.data || result.count > 4096 ||
        result.first > UINT32_MAX - result.count)
        return {};
    std::uintptr_t fractions{};
    if (!memory::read(result.data + 8, fractions) || !fractions)
        return {};
    float closest = 2;
    for (std::uint32_t n = 0; n < result.count; ++n) {
        float fraction{};
        if (!memory::read(fractions + static_cast<std::uintptr_t>(result.first + n) * 4, fraction))
            return {};
        if (std::isfinite(fraction) && fraction >= 0 && fraction <= 1 && fraction < closest) {
            const auto hit_entity = owner ? owner(world, result.first + n, result.data) : 0;
            if (hit_entity && std::find(ignored.begin(), ignored.end(), hit_entity) != ignored.end()) {
                SurfaceBody body{};
                if (body_reader && body_reader(world, result.first + n, result.data, body) &&
                    body[0] == world)
                    discovered.push_back(body);
                continue;
            }
            closest = fraction;
            if (entity)
                *entity = hit_entity;
        }
    }
    if (closest > 1)
        return {};
    return add(start, mul(sub(end, start), closest));
}
inline std::optional<Vec3> cast_surface(const NativeSurfaceApi &api, std::uintptr_t world, Vec3 start,
                                        Vec3 end, SurfaceOwner owner = nullptr,
                                        std::span<const std::uint64_t> ignored = {},
                                        std::uint64_t *entity = nullptr,
                                        std::span<const SurfaceBody> ignored_bodies = {},
                                        SurfaceBodyReader body_reader = nullptr) {
    std::vector<SurfaceBody> bodies(ignored_bodies.begin(), ignored_bodies.end());
    // A closest-hit query may initially return only the selected object's body.
    // Learn that exact body from the hit, exclude it natively, then query again.
    // This also covers multiple collision children and newly replicated previews.
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        if (entity)
            *entity = 0;
        std::vector<SurfaceBody> discovered;
        if (const auto hit = cast_surface_once(api, world, start, end, owner, ignored, entity, bodies,
                                               discovered, body_reader))
            return hit;
        const auto count = bodies.size();
        for (const auto &body : discovered)
            if (std::find(bodies.begin(), bodies.end(), body) == bodies.end())
                bodies.push_back(body);
        if (bodies.size() == count || bodies.size() > 4096)
            break;
    }
    return {}; // Hold the preview when exclusions cannot establish a real surface.
}
inline EditorSurfaceHit probe_surface(const NativeSurfaceApi &api, std::uintptr_t context,
                                      const EditorSurfaceRequest &request, SurfaceOwner owner = nullptr,
                                      std::span<const std::uint64_t> ignored = {},
                                      std::span<const SurfaceBody> ignored_bodies = {},
                                      SurfaceBodyReader body_reader = nullptr) {
    EditorSurfaceHit result{request.generation, request.id};
    if (!api.ready || !context || !request.id || !std::isfinite(request.grid) || request.grid < 0 ||
        request.grid > 100)
        return result;
    for (float value : request.origin)
        if (!std::isfinite(value) || std::abs(value) > 100000)
            return result;
    const auto direction = normalized(request.direction);
    if (!std::isfinite(dot(direction, direction)) || dot(direction, direction) < .9f)
        return result;
    const auto world = api.world(context);
    if (!world)
        return result;
    result.available = true;
    auto hit = cast_surface(api, world, request.origin, add(request.origin, mul(direction, 500)), owner,
                            ignored, &result.entity, ignored_bodies, body_reader);
    if (hit && request.grid > 0) {
        const float x = snapped((*hit)[0], request.grid), z = snapped((*hit)[2], request.grid);
        // Snap horizontal coordinates, then query terrain again. Rounding Y
        // would float objects above slopes or bury them below the real ground.
        hit = cast_surface(api, world, {x, (*hit)[1] + 10, z}, {x, (*hit)[1] - 10, z}, owner, ignored,
                           nullptr, ignored_bodies, body_reader);
    }
    if (hit) {
        result.hit = true;
        result.position = *hit;
    }
    return result;
}
} // namespace dingosdk::editor

