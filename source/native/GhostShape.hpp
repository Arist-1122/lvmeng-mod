#pragma once
#include "ia_snapshot_v1.h"
#include <cstdint>

namespace ghost_shape {
struct Mesh {
    uint32_t vertices;
    uint32_t indices;
    uint32_t metadata_stride;
    bool shared_face;
};
inline constexpr Mesh meshes[] = {
    {34759, 118446, 16, false},
    {13657, 50178, 12, false},
    {4637, 17787, 16, false},
    {8041, 33582, 16, false},
    {2533, 11106, 16, true},
};

// The caller first verifies exact VS/PS, target-outfit foreground lease and
// root signature. This checks full direct geometry views in one recording.
inline bool matches(const rs_ia_snapshot_v1 &ia, uint64_t root_recording,
                    uint32_t count, uint32_t instances, uint32_t first,
                    int32_t base, uint32_t first_instance, bool allow_shared_face) {
    if (ia.status != rs_ia_complete || ia.command_list_type != 0 ||
        !root_recording || ia.recording_generation != root_recording ||
        (ia.vertex_known_mask & 3) != 3 || !ia.index_known || !ia.index_present ||
        !ia.index.buffer_location || ia.index.format != 57 ||
        instances != 1 || first || base || first_instance)
        return false;
    for (const auto &mesh : meshes) {
        if (count != mesh.indices || (mesh.shared_face && !allow_shared_face))
            continue;
        const auto &position_normal = ia.vertex[0];
        const auto &metadata = ia.vertex[1];
        return ia.index.size_bytes == count * 2 &&
               position_normal.buffer_location && metadata.buffer_location &&
               position_normal.stride_bytes == 24 &&
               position_normal.size_bytes == mesh.vertices * 24 &&
               metadata.stride_bytes == mesh.metadata_stride &&
               metadata.size_bytes == mesh.vertices * mesh.metadata_stride;
    }
    return false;
}
} // namespace ghost_shape
