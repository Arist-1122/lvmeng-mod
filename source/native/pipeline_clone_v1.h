#pragma once
#include <stdint.h>
enum rs_pipeline_clone_status_v1 : uint32_t {
 rs_clone_none=0, rs_clone_created=1, rs_clone_invalid=2,
 rs_clone_not_current=3, rs_clone_modified_route=4,
 rs_clone_unsupported_stream=5, rs_clone_native_failed=6, rs_clone_budget=7
};
struct rs_pipeline_clone_result_v1 {
 uint32_t size,version,status;int32_t native_result;
 uint64_t original_pipeline,clone_pipeline;
 uint32_t stream_bytes,pixel_bytecode_offset,original_vs_crc32,original_ps_crc32;
 uint32_t replacement_ps_crc32,non_ps_stream_crc32,non_ps_bytes_equal,reserved;
};
static_assert(sizeof(rs_pipeline_clone_result_v1)==64,"Clone result ABI");
// Optional C __cdecl export, only inside the synchronous init_pipeline callback
// for this exact device and original pipeline on the same thread:
// bool ReShadeD3D12CloneCurrentPipelinePixelShaderV1(void *api_device,
//   uint64_t original_pipeline,const void *replacement_dxbc,
//   uint64_t replacement_bytes,rs_pipeline_clone_result_v1 *out);
// size/version must be initialized. false means bad ABI/arguments. true means
// processed: inspect status. This creates a caller-owned native PSO reference,
// without addon create/init callbacks. The caller releases it after GPU use.
// At most four native attempts per callback scope. Only the unmodified native
// CreatePipelineState path is supported; API20 reconstruction and loaded PSOs
// are rejected. Nonempty CachedPSO, unknown/duplicate stream fields, compute or
// mesh pipeline streams are rejected. No input pointer is retained after return.
// The PS bytecode pointer+length alone are replaced in a bounded 8192-byte
// aligned stream copy. All non-PS stream bytes, including native view pointers,
// SO, sample quality, node mask, strip cut and flags are preserved exactly.
// Nested init callbacks have their own scope; an outer scope is restored only
// when the nested scope ends. The ABI grants no Game targeting authorization.
