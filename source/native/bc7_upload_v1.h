#pragma once
#include <stdint.h>

// Optional exports scoped to the current copy_buffer_to_texture callback.
// An eligible view is borrowed. The add-on must compare the source bytes and
// retain the replacement upload resource while recorded GPU work can use it.
enum rs_bc7_upload_status_v1 : uint32_t {
    rs_bc7_upload_not_current = 0,
    rs_bc7_upload_eligible = 1,
    rs_bc7_upload_wrong_shape = 2,
    rs_bc7_upload_partial = 3,
    rs_bc7_upload_wrong_footprint = 4,
    rs_bc7_upload_wrong_heap = 5,
    rs_bc7_upload_bounds = 6,
    rs_bc7_upload_unsupported_list = 7,
};
struct rs_bc7_upload_view_v1 {
    uint32_t size, version, status, subresource;
    uint32_t texture_width, texture_height, mip_count, texture_format;
    uint32_t footprint_width, footprint_height, footprint_format, row_pitch;
    uint32_t row_bytes, block_rows, reserved[2];
    uint64_t token, source_resource, destination_resource, source_offset, source_span;
};
static_assert(sizeof(rs_bc7_upload_view_v1) == 104);
struct rs_bc7_upload_replacement_v1 {
    uint32_t size, version;
    uint64_t token, upload_resource, upload_offset;
};
static_assert(sizeof(rs_bc7_upload_replacement_v1) == 32);
// bool ReShadeD3D12QueryCanonicalBC7UploadV1(void *api_command_list,
//     rs_bc7_upload_view_v1 *out);
// bool ReShadeD3D12ReplaceCanonicalBC7UploadV1(void *api_command_list,
//     const rs_bc7_upload_replacement_v1 *replacement);
// Replace schedules the source for this call; the callback must return false
// to let the native copy execute. It does not write source/target bytes.
