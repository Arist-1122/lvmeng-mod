// Executes the unchanged production SurfaceLightRuntime.inl with a small API
// shim. It tests dispatch/control/lifetime logic, not native upload attribution.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <atomic>
#include <stdexcept>
#include <iostream>

namespace api {
enum class shader_stage:uint32_t{pixel=16};
enum class descriptor_type:uint32_t{shader_resource_view};
enum class pipeline_layout_param_type:uint32_t{descriptor_table,push_descriptors_with_ranges,descriptor_table_with_flags,push_descriptors_with_ranges_and_flags,push_descriptors};
enum class pipeline_subobject_type:uint32_t{vertex_shader,pixel_shader,hull_shader,domain_shader,geometry_shader};
enum class pipeline_stage:uint32_t{vertex_shader=1,all_graphics=0xffffffff};
enum class dynamic_state:uint32_t{primitive_topology};
enum class primitive_topology:uint32_t{triangle_list=3};
struct pipeline{uint64_t handle=0;};struct pipeline_layout{uint64_t handle=0;};
struct descriptor_range{shader_stage visibility=shader_stage::pixel;uint32_t dx_register_space=0;descriptor_type type=descriptor_type::shader_resource_view;uint32_t dx_register_index=0,count=0,array_size=1,binding=0;};
struct descriptor_range_with_flags:descriptor_range{};
struct pipeline_layout_param{pipeline_layout_param_type type;struct{unsigned count;const descriptor_range*ranges;}descriptor_table;struct{unsigned count;const descriptor_range_with_flags*ranges;}descriptor_table_with_flags;descriptor_range push_descriptors;};
struct shader_desc{const void*code=nullptr;size_t code_size=0;};
struct pipeline_subobject{pipeline_subobject_type type;void*data=nullptr;uint32_t count=0;};
struct device{std::vector<uint64_t>released;void destroy_pipeline(pipeline p){released.push_back(p.handle);}};
struct command_list{
 device*dev=nullptr;unsigned binds=0,draws=0;uint64_t native_pipeline=0;uint32_t indices=0,instances=0,first=0,first_instance=0;int32_t base=0;
 device*get_device(){return dev;}
 void bind_pipeline(pipeline_stage,pipeline p){++binds;native_pipeline=p.handle;}
 void draw_indexed(uint32_t n,uint32_t i,uint32_t f,int32_t b,uint32_t fi){++draws;indices=n;instances=i;first=f;base=b;first_instance=fi;}
};
}
namespace combo_hair{thread_local bool internal=false;}
namespace head_upload{bool CheckSha256(const void*,uint32_t,const char*){return false;}}
const wchar_t package_root[]=L"probe-not-used";
bool read_bounded(const wchar_t*,void*,DWORD,DWORD&){return false;}
std::atomic<api::device*>texture_device{nullptr};
std::atomic<uint64_t>primary_body_seen{0};bool lease_active=true;
bool can_replace(){return lease_active;}
struct Matcher{int owner=-1;int ConfirmedFullTexture(uint64_t id){return id==0x9000?owner:-1;}}matcher;
#include "../native/SurfaceLightRuntime.inl"

static unsigned assertions=0;
void require(bool ok,const char*name){++assertions;if(!ok)throw std::runtime_error(name);}
rs_graphics_root_snapshot_v1 current_root;
rs_descriptor_metadata_v1 current_view;
unsigned roots=0,views=0;bool root_mutation=false,view_mutation=false;
bool root_query(void*,uint64_t,uint32_t,rs_graphics_root_snapshot_v1*out){*out=current_root;if(root_mutation&&++roots==2)++out->recording_generation;return true;}
bool view_query(void*,uint64_t,uint32_t,uint32_t,rs_descriptor_metadata_v1*out){*out=current_view;if(view_mutation&&++views==2)++out->slot_epoch;return true;}

int main(){
 try{
  api::device device,other;api::command_list list;list.dev=&device;texture_device.store(&device);
  surface_light::query_root=root_query;surface_light::query_descriptor=view_query;surface_light::cloned.store(1);
  unsigned positives=0,negatives=0,lifetimes=0;
  auto setup=[&](unsigned program,int owner){
   using namespace surface_light;
   const auto&asset=surface_light_programs[program];pipelines.clear();for(auto&l:layouts)l={};for(auto&c:commands)c={};
   layouts[0].id=123;layouts[0].gen=7;const unsigned slot=slot_for(asset.texture_register);layouts[0].slots[slot]={9,asset.texture_register,true};
   Pipeline pipeline;pipeline.id=456;pipeline.gen=8;pipeline.layout=123;pipeline.layout_gen=7;pipeline.count=1;pipeline.variants[0]={uint64_t(10000+program),program};pipelines.emplace(pipeline.id,pipeline);
   commands[0]={&list,456,8,true};list={};list.dev=&device;list.native_pipeline=456;
   matcher.owner=owner;lease_active=true;internal=false;combo_hair::internal=false;roots=views=0;root_mutation=view_mutation=false;
   current_root={sizeof(current_root),1};current_root.status=rs_root_complete;current_root.recording_generation=100;current_root.command_list_type=D3D12_COMMAND_LIST_TYPE_DIRECT;current_root.root_signature_known=1;current_root.root_signature=123;
   current_root.heap_known_mask=1;current_root.parameter_known_mask=1ULL<<9;current_root.native_heaps[0]=0x8800;current_root.heap_gpu_starts[0]=0x20000;current_root.heap_descriptor_increments[0]=16;current_root.heap_descriptor_counts[0]=64;current_root.parameters[9]={0x20020,rs_root_parameter_table,0};
   current_view={sizeof(current_view),1};current_view.status=rs_descriptor_known;current_view.kind=rs_descriptor_srv;current_view.heap_type=0;current_view.view_dimension=D3D12_SRV_DIMENSION_TEXTURE2D;current_view.format=DXGI_FORMAT_BC7_UNORM_SRGB;
   current_view.heap_slot=2+asset.texture_register;current_view.heap_generation=1;current_view.resource_generation=2;current_view.slot_epoch=3;current_view.resource_identity=0x9000;
  };
  for(unsigned p=0;p<surface_light::program_count;++p){
   const auto&asset=surface_light_programs[p];int owner=0;while(!(asset.owners&(1U<<owner)))++owner;
   const uint32_t count=asset.indices[owner]?asset.indices[owner]:300;
   for(int o=0;o<6;++o){
    setup(p,o);const bool allowed=(asset.owners&(1U<<o))!=0;const uint32_t n=allowed&&asset.indices[o]?asset.indices[o]:count;
    const bool result=surface_light::draw(&list,n,1,0,0,0);
    require(result==allowed,"owner dispatch");require(list.draws==unsigned(allowed),"exactly one replacement draw");require(list.binds==unsigned(allowed?2:0),"bind and restore");require(list.native_pipeline==456,"original pipeline restored");
    if(allowed){++positives;require(list.indices==n&&list.instances==1,"original draw arguments");}else ++negatives;
   }
   auto deny=[&](auto change){setup(p,owner);change();require(!surface_light::draw(&list,count,1,0,0,0),"failed guard rejected");require(list.draws==0&&list.binds==0,"failed guard untouched");++negatives;};
   deny([&]{matcher.owner=-1;});deny([&]{lease_active=false;});deny([&]{list.dev=&other;});deny([&]{combo_hair::internal=true;});deny([&]{surface_light::internal=true;});
   deny([&]{current_root.recording_generation=0;});deny([&]{current_root.command_list_type=D3D12_COMMAND_LIST_TYPE_BUNDLE;});deny([&]{current_root.root_signature=124;});deny([&]{current_root.heap_known_mask=0;});deny([&]{current_root.parameter_known_mask=0;});
   deny([&]{current_view.status=rs_descriptor_busy;});deny([&]{current_view.format=DXGI_FORMAT_BC7_UNORM;});deny([&]{current_view.resource_identity=0x9999;});deny([&]{current_view.resource_generation=0;});deny([&]{current_view.slot_epoch=0;});
   deny([&]{root_mutation=true;});deny([&]{view_mutation=true;});deny([&]{surface_light::layouts[0].gen++;});deny([&]{surface_light::commands[0].gen++;});deny([&]{surface_light::commands[0].triangle=false;});
   setup(p,owner);require(!surface_light::draw(&list,count,2,0,0,0),"instances rejected");++negatives;
   setup(p,owner);require(!surface_light::draw(&list,count,1,0,0,1),"instance offset rejected");++negatives;
   if(asset.indices[owner]){setup(p,owner);require(!surface_light::draw(&list,count+3,1,0,0,0),"shape rejected");++negatives;}
  }
  // The same original 9F PSO must select different Body01/02 candidates.
  setup(3,4);auto&row=surface_light::pipelines.at(456);row.count=2;row.variants[1]={10004,4};surface_light::layouts[0].slots[1]={9,6,true};
  require(surface_light::draw(&list,50178,1,0,0,0),"Body02 second variant");require(list.draws==1,"Body02 single draw");++positives;
  // Face submeshes retain their nonzero original first index and base vertex.
  setup(5,2);require(surface_light::draw(&list,300,1,702,4,0),"Face submesh");require(list.first==702&&list.base==4,"Face arguments preserved");++positives;
  // Destroy/recreate more than the previous lifetime quota; every clone owned
  // by this subsystem must be released and its active family slot returned.
  for(unsigned cycle=0;cycle<40;++cycle){
   setup(0,0);surface_light::family_clones[0]=1;size_t before=device.released.size();surface_light::destroy_pipeline(&device,{456});
   require(surface_light::family_clones[0]==0,"family quota returned");require(surface_light::pipelines.empty(),"original row erased");require(device.released.size()==before+1&&device.released.back()==10000,"clone reference released");++lifetimes;
  }
  setup(0,0);surface_light::family_clones[0]=1;surface_light::destroy_device(&device);require(surface_light::pipelines.empty()&&surface_light::family_clones[0]==0,"device cleanup");
  require(!surface_light::internal&&!combo_hair::internal,"internal guard restored");
  {surface_light::InternalScope a;require(surface_light::internal&&combo_hair::internal,"both guards active");{surface_light::InternalScope b;require(surface_light::internal&&combo_hair::internal,"nested guard");}require(surface_light::internal&&combo_hair::internal,"nested restores outer");}
  require(!surface_light::internal&&!combo_hair::internal,"outer guard restored");
  std::cout<<"{\"passed\":true,\"gameUsed\":false,\"productionInlExecutedWithApiShim\":true,\"nativeCoreAttributionTested\":false,\"programs\":21,\"positiveDispatches\":"<<positives<<",\"negativeDispatches\":"<<negatives<<",\"lifetimeCycles\":"<<lifetimes<<",\"assertions\":"<<assertions<<"}\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 3;}
}
