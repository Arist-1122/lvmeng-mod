#pragma once
#include <stdint.h>

// Optional fork extension. It does not change the ReShade API20 ABI.
// Call only while the api::device pointer is valid (for example in a callback).
// This copies metadata and returns no ownership of resources or heaps.
// Neither a successful query nor native-call completion proves GPU consumption.
enum rs_descriptor_status_v1 : uint32_t
{
    rs_descriptor_unknown = 0,
    rs_descriptor_known = 1,
    rs_descriptor_null = 2,
    rs_descriptor_busy = 3,
    rs_descriptor_capacity = 4,
    rs_descriptor_invalid = 5,
    rs_descriptor_dead = 6,
    rs_descriptor_unsupported = 7,
};
enum rs_descriptor_kind_v1 : uint32_t
{
    rs_descriptor_kind_unknown = 0,
    rs_descriptor_srv = 1,
    rs_descriptor_uav = 2,
    rs_descriptor_cbv = 3,
    rs_descriptor_sampler = 4,
    rs_descriptor_rtv = 5,
    rs_descriptor_dsv = 6,
};
struct rs_descriptor_metadata_v1
{
    uint32_t size;              // Caller sets sizeof(rs_descriptor_metadata_v1).
    uint32_t version;           // Caller sets 1. Unsupported size/version rejected.
    uint32_t status;
    uint32_t kind;
    uint32_t heap_type;         // Native D3D12_DESCRIPTOR_HEAP_TYPE.
    uint32_t view_dimension;    // Native SRV/UAV dimension when applicable.
    uint32_t format;            // Native DXGI_FORMAT.
    uint32_t stride;            // Structured buffer byte stride; otherwise zero.
    uint64_t heap_generation;
    uint64_t resource_generation;
    uint64_t mutation_epoch;
    uint64_t slot_epoch;
    uint64_t heap_slot;
    uint64_t resource_identity; // Identity only. Do not dereference/AddRef this result.
    uint64_t resource_size;     // Buffer bytes, zero for non-buffer/unknown.
    uint64_t offset_bytes;
    uint64_t element_count;
    uint64_t range_bytes;
};
static_assert(sizeof(rs_descriptor_metadata_v1) == 112, "Stable x64 optional ABI");
// Export signature (C linkage, __cdecl):
// bool ReShadeD3D12QueryDescriptorMetadataV1(void *api_device,
//     uint64_t table, uint32_t binding, uint32_t array_offset,
//     rs_descriptor_metadata_v1 *out_metadata);
// false = invalid arguments, unsupported size/version, or non-D3D12 device.
// true = query processed; inspect status. Only known+srv+structured stride16
// and caller-validated layout/range/generation is a palette metadata candidate.
// No API20 getter fallback is permitted for unknown/busy/capacity/dead states.
