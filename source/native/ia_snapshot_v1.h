#pragma once
#include <stdint.h>

// Optional v4 fork extension, not exported by core v3. No API20 vtable edit.
// Only CPU metadata from instrumented native/API calls is observed. No resource
// ownership, native descriptor read, command submission or GPU completion proof.
enum rs_ia_status_v1 : uint32_t {
 rs_ia_untracked=0, rs_ia_complete=1, rs_ia_partial=2, rs_ia_busy=3,
 rs_ia_closed=4, rs_ia_unsupported=5, rs_ia_invalid=6, rs_ia_destroyed=7
};
struct rs_vertex_buffer_view_v1 { uint64_t buffer_location; uint32_t size_bytes,stride_bytes; };
struct rs_index_buffer_view_v1 { uint64_t buffer_location; uint32_t size_bytes,format; };
struct rs_ia_snapshot_v1 {
 uint32_t size,version,status,command_list_type;
 uint64_t recording_generation,mutation_epoch;
 uint32_t vertex_known_mask,index_known,index_present,reserved;
 rs_vertex_buffer_view_v1 vertex[32];
 rs_index_buffer_view_v1 index;
};
static_assert(sizeof(rs_vertex_buffer_view_v1)==16,"VBV wire size");
static_assert(sizeof(rs_index_buffer_view_v1)==16,"IBV wire size");
static_assert(sizeof(rs_ia_snapshot_v1)==576,"Snapshot wire size");
struct rs_ia_binding_v1 {
 uint32_t size,version,first_vertex_slot,vertex_count;
 uint64_t expected_recording_generation;
 uint32_t bind_index,index_present;
 rs_vertex_buffer_view_v1 vertex[32];
 rs_index_buffer_view_v1 index;
};
static_assert(sizeof(rs_ia_binding_v1)==560,"Exact binding wire size");
// Proposed C export, __cdecl:
// bool ReShadeD3D12QueryIASnapshotV1(void *api_command_list,
//   uint32_t required_vertex_mask, uint32_t require_index,
//   rs_ia_snapshot_v1 *out_snapshot);
// Caller initializes size/version, passes a live D3D12 API command-list pointer,
// and serializes recording on this list. require_index is 0 or 1.
// false: bad arguments/size/version/API. true: processed; inspect status/masks.
// Complete means all requested observed views are known, not all 32 necessarily.
// Missing views are zero-filled and never guessed from resource Width-offset.
// Index-known + !index-present means an explicitly observed null IB binding.
// Snapshot generations/epochs must match the current recording. Native writes
// bypassing the instrumented proxy/API path are outside this observed-state ABI.
// Optional mutation export (unlike the metadata-only query):
// bool ReShadeD3D12BindIAViewsV1(void *api_command_list,
//   const rs_ia_binding_v1 *binding);
// Requires exact nonzero recording generation and a supported, nonbusy list.
// Calls native IASetVB/IB with exact values, updates this tracker, emits no addon
// bind events. Caller owns all resources and serializes recording. Zero-count
// with bind_index=0 is inert. index_present=0 binds a null native index view.
