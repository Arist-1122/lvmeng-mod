// Include after the existing texture matcher, lease helpers and ComboHair.inl.
// Register these callbacks alongside the existing callbacks. Call draw first;
// false delegates the original draw to the existing combat/hair implementation.
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "descriptor_metadata_v1.h"
#include "graphics_root_snapshot_v1.h"
#include "pipeline_clone_v1.h"
#include "HairScope.hpp"
#include "SurfaceLight.generated.hpp"

namespace surface_light {
using Root=bool(__cdecl*)(void*,uint64_t,uint32_t,rs_graphics_root_snapshot_v1*);
using Descriptor=bool(__cdecl*)(void*,uint64_t,uint32_t,uint32_t,rs_descriptor_metadata_v1*);
using Clone=bool(__cdecl*)(void*,uint64_t,const void*,uint64_t,rs_pipeline_clone_result_v1*);
Root query_root=nullptr;Descriptor query_descriptor=nullptr;Clone clone=nullptr;
constexpr unsigned program_count=sizeof(surface_light_programs)/sizeof(surface_light_programs[0]);
constexpr unsigned registers[]={4,6,7,8};
constexpr unsigned family_limit=16;
std::unique_ptr<unsigned char[]> shaders[program_count];
std::atomic<uint64_t> clone_attempts{0},cloned{0},eligible{0},owner_rejected{0},drawn{0};
std::atomic<uint64_t> owner_draws[6];
unsigned family_clones[program_count]={};
std::mutex state;thread_local bool internal=false;
// Both subsystems hold independent state locks. Internal graphics operations
// must be invisible to BOTH callback sets to avoid opposite lock acquisition.
struct InternalScope {
 bool previous_surface=internal,previous_combo=combo_hair::internal;
 InternalScope(){internal=true;combo_hair::internal=true;}
 ~InternalScope(){combo_hair::internal=previous_combo;internal=previous_surface;}
 InternalScope(const InternalScope&)=delete;
 InternalScope&operator=(const InternalScope&)=delete;
};
uint64_t generation=0;
struct Binding{uint32_t param=0,binding=0;bool known=false;};
struct Layout{uint64_t id=0,gen=0;Binding slots[4];};
struct Variant{uint64_t clone=0;unsigned program=0;};
struct Pipeline{uint64_t id=0,gen=0,layout=0,layout_gen=0;Variant variants[2];unsigned count=0;};
struct Command{api::command_list* id=nullptr;uint64_t pipeline=0,gen=0;bool triangle=false;};
Layout layouts[256];Command commands[512];
std::unordered_map<uint64_t,Pipeline> pipelines;

template<typename T,size_t N>T* find(T(&rows)[N],uint64_t id){
 if(id)for(auto&row:rows)if(row.id==id)return &row;return nullptr;
}
Pipeline* find(uint64_t id){auto p=pipelines.find(id);return p==pipelines.end()?nullptr:&p->second;}
Command* command(api::command_list*c,bool create=false){
 if(!c)return nullptr;Command* empty=nullptr;
 for(auto&row:commands){if(row.id==c)return &row;if(!row.id&&!empty)empty=&row;}
 if(create&&empty){*empty={};empty->id=c;return empty;}return nullptr;
}
unsigned slot_for(uint32_t reg){for(unsigned i=0;i<4;++i)if(registers[i]==reg)return i;return 4;}

bool initialize(HMODULE core){
 query_root=reinterpret_cast<Root>(GetProcAddress(core,"ReShadeD3D12QueryGraphicsRootStateV1"));
 query_descriptor=reinterpret_cast<Descriptor>(GetProcAddress(core,"ReShadeD3D12QueryDescriptorMetadataV1"));
 clone=reinterpret_cast<Clone>(GetProcAddress(core,"ReShadeD3D12CloneCurrentPipelinePixelShaderV1"));
 if(!query_root||!query_descriptor||!clone)return false;
 for(unsigned i=0;i<program_count;++i){
  const auto&p=surface_light_programs[i];wchar_t path[MAX_PATH];DWORD count=0;
  if(!p.bytes||p.bytes>1024*1024||swprintf_s(path,L"%s\\%s",package_root,p.path)<0)return false;
  shaders[i].reset(new(std::nothrow)unsigned char[p.bytes]);
  if(!shaders[i]||!read_bounded(path,shaders[i].get(),p.bytes,count)||count!=p.bytes||
     !head_upload::CheckSha256(shaders[i].get(),count,p.sha))return false;
 }return true;
}

void init_layout(api::device*d,uint32_t n,const api::pipeline_layout_param*params,api::pipeline_layout id){
 if(internal||combo_hair::internal||!id.handle||!params||n>64||d!=texture_device.load(std::memory_order_acquire))return;
 Layout candidate;candidate.id=id.handle;unsigned found[4]={};
 auto inspect=[&](uint32_t param,const api::descriptor_range&r,bool direct){
  if(!(uint32_t(r.visibility)&uint32_t(api::shader_stage::pixel))||r.dx_register_space||r.type!=api::descriptor_type::shader_resource_view)return;
  for(unsigned i=0;i<4;++i){
   const unsigned reg=registers[i];if(r.dx_register_index>reg||reg-r.dx_register_index>=r.count)continue;
   const unsigned offset=reg-r.dx_register_index;
   if(direct||r.array_size!=1||r.count>1024||r.binding>UINT32_MAX-offset){found[i]=2;continue;}
   ++found[i];candidate.slots[i]={param,r.binding+offset,false};
  }
 };
 for(unsigned i=0;i<n;++i){const auto&p=params[i];switch(p.type){
  case api::pipeline_layout_param_type::descriptor_table:
  case api::pipeline_layout_param_type::push_descriptors_with_ranges:
   if(p.descriptor_table.count>128||!p.descriptor_table.ranges)return;
   for(unsigned j=0;j<p.descriptor_table.count;++j)inspect(i,p.descriptor_table.ranges[j],p.type!=api::pipeline_layout_param_type::descriptor_table);break;
  case api::pipeline_layout_param_type::descriptor_table_with_flags:
  case api::pipeline_layout_param_type::push_descriptors_with_ranges_and_flags:
   if(p.descriptor_table_with_flags.count>128||!p.descriptor_table_with_flags.ranges)return;
   for(unsigned j=0;j<p.descriptor_table_with_flags.count;++j)inspect(i,p.descriptor_table_with_flags.ranges[j],p.type!=api::pipeline_layout_param_type::descriptor_table_with_flags);break;
  case api::pipeline_layout_param_type::push_descriptors:inspect(i,p.push_descriptors,true);break;
  default:break;
 }}
 std::lock_guard<std::mutex>guard(state);if(auto old=find(layouts,id.handle))*old={};
 for(unsigned i=0;i<4;++i)candidate.slots[i].known=found[i]==1;
 for(auto&row:layouts)if(!row.id){candidate.gen=++generation;row=candidate;return;}
}
void destroy_layout(api::device*d,api::pipeline_layout id){
 if(internal||combo_hair::internal||d!=texture_device.load(std::memory_order_acquire))return;
 std::lock_guard<std::mutex>guard(state);if(auto row=find(layouts,id.handle))*row={};
}
void init_pipeline(api::device*d,api::pipeline_layout layout,uint32_t n,const api::pipeline_subobject*objects,api::pipeline id){
 if(internal||combo_hair::internal||!id.handle||!objects||d!=texture_device.load(std::memory_order_acquire))return;
 const api::shader_desc*vs=nullptr,*ps=nullptr;
 for(unsigned i=0;i<n;++i)if(objects[i].data&&objects[i].count==1){
  const auto kind=objects[i].type;
  if(kind==api::pipeline_subobject_type::vertex_shader)vs=static_cast<const api::shader_desc*>(objects[i].data);
  if(kind==api::pipeline_subobject_type::pixel_shader)ps=static_cast<const api::shader_desc*>(objects[i].data);
  if((kind==api::pipeline_subobject_type::hull_shader||kind==api::pipeline_subobject_type::domain_shader||kind==api::pipeline_subobject_type::geometry_shader)&&static_cast<const api::shader_desc*>(objects[i].data)->code_size)return;
 }
 if(!vs||!ps||!vs->code||!ps->code)return;
 unsigned matches[2],count=0;
 for(unsigned i=0;i<program_count;++i){const auto&p=surface_light_programs[i];
  if(vs->code_size!=p.vs_bytes||ps->code_size!=p.ps_bytes||!head_upload::CheckSha256(vs->code,uint32_t(vs->code_size),p.vs_sha)||!head_upload::CheckSha256(ps->code,uint32_t(ps->code_size),p.ps_sha))continue;
  if(count==2)return;matches[count++]=i;
 }
 if(!count)return;
 std::lock_guard<std::mutex>guard(state);const auto*l=find(layouts,layout.handle);
 if(!l||find(id.handle)||pipelines.size()>=512)return;
 Pipeline row;row.id=id.handle;row.gen=++generation;row.layout=layout.handle;row.layout_gen=l->gen;
 for(unsigned j=0;j<count;++j){const unsigned index=matches[j];const auto&p=surface_light_programs[index];
  const unsigned slot=slot_for(p.texture_register);if(slot==4||!l->slots[slot].known||family_clones[index]>=family_limit)continue;
  rs_pipeline_clone_result_v1 proof={sizeof(proof),1};bool ok=false;
  {InternalScope scope;ok=clone(d,id.handle,shaders[index].get(),p.bytes,&proof);}
  clone_attempts.fetch_add(1,std::memory_order_relaxed);
  if(!ok||proof.status!=rs_clone_created||!proof.clone_pipeline||!proof.non_ps_bytes_equal||proof.original_vs_crc32!=p.vs_crc||proof.original_ps_crc32!=p.ps_crc||proof.replacement_ps_crc32!=p.crc){
   if(proof.status==rs_clone_created&&proof.clone_pipeline){InternalScope scope;d->destroy_pipeline({proof.clone_pipeline});}
   continue;
  }
  row.variants[row.count++]={proof.clone_pipeline,index};++family_clones[index];cloned.fetch_add(1,std::memory_order_release);
 }
 if(row.count)pipelines.emplace(id.handle,row);
}
void destroy_pipeline(api::device*d,api::pipeline id){
 if(internal||combo_hair::internal||d!=texture_device.load(std::memory_order_acquire))return;
 Variant retired[2];unsigned count=0;
 {
  std::lock_guard<std::mutex>guard(state);auto row=pipelines.find(id.handle);if(row==pipelines.end())return;
  count=row->second.count;
  for(unsigned i=0;i<count;++i){retired[i]=row->second.variants[i];if(family_clones[retired[i].program])--family_clones[retired[i].program];}
  pipelines.erase(row);
 }
 // CloneCurrentPipeline transfers one native PSO reference to the caller.
 // The engine's original-PSO destruction is the matching GPU lifetime point.
 // Release outside our state lock, then let a later PSO reuse the family slot.
 InternalScope scope;for(unsigned i=0;i<count;++i)d->destroy_pipeline({retired[i].clone});
}
void destroy_device(api::device*d){
 if(internal||combo_hair::internal||d!=texture_device.load(std::memory_order_acquire))return;
 std::vector<uint64_t> retired;
 {
  std::lock_guard<std::mutex>guard(state);
  for(const auto&entry:pipelines)for(unsigned i=0;i<entry.second.count;++i)retired.push_back(entry.second.variants[i].clone);
  pipelines.clear();for(auto&n:family_clones)n=0;for(auto&l:layouts)l={};for(auto&c:commands)c={};
 }
 InternalScope scope;for(auto id:retired)d->destroy_pipeline({id});
}
void reset(api::command_list*c){if(internal||combo_hair::internal)return;std::lock_guard<std::mutex>guard(state);if(auto row=command(c))*row={};}
void secondary(api::command_list*c,api::command_list*){reset(c);}
void bind(api::command_list*c,api::pipeline_stage stage,api::pipeline id){
 if(internal||combo_hair::internal||!(uint32_t(stage)&uint32_t(api::pipeline_stage::vertex_shader))||c->get_device()!=texture_device.load(std::memory_order_acquire))return;
 std::lock_guard<std::mutex>guard(state);if(auto row=command(c,true)){const auto*p=find(id.handle);row->pipeline=p?p->id:0;row->gen=p?p->gen:0;}
}
void topology(api::command_list*c,uint32_t n,const api::dynamic_state*types,const uint32_t*values){
 if(internal||combo_hair::internal||c->get_device()!=texture_device.load(std::memory_order_acquire))return;
 for(unsigned i=n;i>0;--i)if(types[i-1]==api::dynamic_state::primitive_topology){
  std::lock_guard<std::mutex>guard(state);if(auto row=command(c,true))row->triangle=values[i-1]==uint32_t(api::primitive_topology::triangle_list);return;
 }
}
bool draw(api::command_list*c,uint32_t indices,uint32_t instances,uint32_t first,int32_t base,uint32_t first_instance){
 if(internal||combo_hair::internal||!indices||instances!=1||first_instance||!cloned.load(std::memory_order_acquire)||!can_replace())return false;
 auto*d=texture_device.load(std::memory_order_acquire);if(!d||c->get_device()!=d)return false;
 std::lock_guard<std::mutex>guard(state);const auto*record=command(c);
 if(!record||!record->triangle||!record->gen)return false;
 const auto*p=find(record->pipeline);if(!p||p->gen!=record->gen)return false;
 const auto*l=find(layouts,p->layout);if(!l||l->gen!=p->layout_gen)return false;
 eligible.fetch_add(1,std::memory_order_relaxed);
 for(unsigned v=0;v<p->count;++v){const auto&variant=p->variants[v];const auto&asset=surface_light_programs[variant.program];
  const unsigned slot=slot_for(asset.texture_register);if(slot==4||!l->slots[slot].known)continue;const auto&binding=l->slots[slot];
  rs_graphics_root_snapshot_v1 root={sizeof(root),1},again={sizeof(again),1};const auto mask=1ULL<<binding.param;
  if(!query_root(c,mask,1,&root)||root.status!=rs_root_complete||!root.recording_generation||root.command_list_type!=D3D12_COMMAND_LIST_TYPE_DIRECT||!root.root_signature_known||root.root_signature!=p->layout)continue;
  const auto table=root.parameters[binding.param].value;rs_descriptor_metadata_v1 a={sizeof(a),1},b={sizeof(b),1};
  if(!table||!query_descriptor(d,table,binding.binding,0,&a)||!hair_light::valid_descriptor(root,binding.param,binding.binding,a))continue;
  const int owner=matcher.ConfirmedFullTexture(a.resource_identity);
  if(owner<0||owner>=6||!(asset.owners&(1U<<owner))||(asset.indices[owner]&&indices!=asset.indices[owner]))continue;
  if(!query_descriptor(d,table,binding.binding,0,&b)||!hair_light::same_descriptor(a,b)||!query_root(c,mask,1,&again)||std::memcmp(&root,&again,sizeof(root))||!can_replace())continue;
  {InternalScope scope;c->bind_pipeline(api::pipeline_stage::all_graphics,{variant.clone});c->draw_indexed(indices,instances,first,base,first_instance);c->bind_pipeline(api::pipeline_stage::all_graphics,{p->id});}
  drawn.fetch_add(1,std::memory_order_relaxed);owner_draws[owner].fetch_add(1,std::memory_order_relaxed);
  if(owner==3)primary_body_seen.store(GetTickCount64(),std::memory_order_release);
  return true;
 }
 owner_rejected.fetch_add(1,std::memory_order_relaxed);return false;
}
} // namespace surface_light
