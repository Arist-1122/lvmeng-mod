#pragma once
#include <stdint.h>
// Additive C ABI. Native raw identities are borrowed, not COM weak references.
enum rs_graphics_root_status_v1 : uint32_t {
 rs_root_untracked=0,rs_root_complete=1,rs_root_partial=2,rs_root_busy=3,
 rs_root_closed=4,rs_root_unsupported=5,rs_root_invalid=6,rs_root_destroyed=7
};
enum rs_graphics_root_parameter_kind_v1 : uint32_t {
 rs_root_parameter_unknown=0,rs_root_parameter_table=1,rs_root_parameter_cbv=2,
 rs_root_parameter_srv=3,rs_root_parameter_uav=4
};
struct rs_graphics_root_parameter_v1 {uint64_t value;uint32_t kind,reserved;};
struct rs_graphics_root_snapshot_v1 {
 uint32_t size,version,status,command_list_type;
 uint64_t recording_generation,mutation_epoch,root_signature,root_signature_epoch,heap_epoch;
 uint32_t root_signature_known,heap_known_mask;
 uint64_t parameter_known_mask;
 uint64_t native_heaps[2]; // Indexed by native D3D12 heap type: view=0, sampler=1.
 uint64_t heap_gpu_starts[2];
 uint32_t heap_descriptor_counts[2],heap_descriptor_increments[2];
 rs_graphics_root_parameter_v1 parameters[64];
};
static_assert(sizeof(rs_graphics_root_parameter_v1)==16,"root parameter ABI");
static_assert(sizeof(rs_graphics_root_snapshot_v1)==1144,"graphics root snapshot ABI");
// bool __cdecl ReShadeD3D12QueryGraphicsRootStateV1(void *live_api_command_list,
//    uint64_t required_parameter_mask,uint32_t required_heap_mask,
//    rs_graphics_root_snapshot_v1 *out);
// out.size=1144 and version=1; heap mask bits 0/1 only. false=bad arguments.
// true=processed, inspect status. Complete requires a known root signature plus
// the requested parameter/heap bits. Known null root/heap/value is represented
// as zero and still must be rejected where a live binding is required.
// Values mean table GPU descriptor handle or root CBV/SRV/UAV GPU VA by kind.
// Root constants clear the affected parameter observation (unsupported kind).
// Different root signature clears all params; redundant identical root keeps
// them. Effective heap changes clear table params; redundant same heap set
// preserves them regardless of input ordering. Reset/ClearState/Bundle/Indirect
// conservatively clear observations. Bundles/internal immediate lists refuse.
// A same-generation query is CPU call-return metadata, never GPU contents.
// Caller serializes recording and independently checks resource/layout/heap
// lifetime generations and table identity against current typed native heaps.
// Heap GPU start/count/increment are observed on native SetDescriptorHeaps;
// they allow bounded table-slot checks without dereferencing snapshot identities.
// External get_native writers are outside the observed-call contract.
