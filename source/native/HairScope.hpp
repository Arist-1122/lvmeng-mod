#pragma once
#include <cstdint>
#include <cstring>

// Gate one native draw on the exact recorded full mip-0 upload, known
// original shader pair, current root table identity and original mesh count.
namespace hair_light {
inline constexpr uint32_t index_count[2]={17787,33582};
inline bool draw_shape(int part,uint32_t count,uint32_t instances,uint32_t first,int32_t base,uint32_t first_instance){
 return part>=0&&part<2&&count==index_count[part]&&instances==1&&!first&&!base&&!first_instance;
}
inline bool same_descriptor(const rs_descriptor_metadata_v1&a,const rs_descriptor_metadata_v1&b) {
 return a.status==b.status&&a.kind==b.kind&&a.heap_type==b.heap_type&&a.format==b.format&&
  a.view_dimension==b.view_dimension&&a.heap_generation==b.heap_generation&&a.resource_generation==b.resource_generation&&
  a.slot_epoch==b.slot_epoch&&a.heap_slot==b.heap_slot&&a.resource_identity==b.resource_identity;
}
inline bool valid_descriptor(const rs_graphics_root_snapshot_v1&r,uint32_t parameter,uint32_t binding,
                             const rs_descriptor_metadata_v1&v){
 if(parameter>=64||!(r.parameter_known_mask&(1ULL<<parameter))||!(r.heap_known_mask&1U)||
  !r.native_heaps[0]||v.status!=rs_descriptor_known||v.kind!=rs_descriptor_srv||v.heap_type!=0||
  v.format!=DXGI_FORMAT_BC7_UNORM_SRGB||v.view_dimension!=D3D12_SRV_DIMENSION_TEXTURE2D||
  !v.heap_generation||!v.resource_generation||!v.resource_identity||!v.slot_epoch)return false;
 const auto &table=r.parameters[parameter];
 const auto start=r.heap_gpu_starts[0];
 const auto stride=r.heap_descriptor_increments[0],size=r.heap_descriptor_counts[0];
 if(table.kind!=rs_root_parameter_table||!table.value||!start||!stride||
  table.value<start||(table.value-start)%stride)return false;
 const auto slot=(table.value-start)/stride;
 return slot<size&&binding<size-slot&&v.heap_slot==slot+binding;
}
}
