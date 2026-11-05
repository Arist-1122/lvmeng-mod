#include "descriptor_metadata_v1.h"
#include "graphics_root_snapshot_v1.h"
#include "pipeline_clone_v1.h"
#include "HairScope.hpp"
#include "GhostShape.hpp"
struct owned_file {const wchar_t* path;uint32_t bytes;const char* sha;};
#include "HairLight.generated.hpp"
#include "GloveLight.generated.hpp"
#include "GroundPrograms.generated.hpp"

namespace combo_hair {
using Root = bool(__cdecl*)(void*,uint64_t,uint32_t,rs_graphics_root_snapshot_v1*);
using Descriptor = bool(__cdecl*)(void*,uint64_t,uint32_t,uint32_t,rs_descriptor_metadata_v1*);
using Clone = bool(__cdecl*)(void*,uint64_t,const void*,uint64_t,rs_pipeline_clone_result_v1*);
using IA=bool(__cdecl*)(void*,uint32_t,uint32_t,rs_ia_snapshot_v1*);
IA query_ia=nullptr;
std::atomic<uint64_t> ghost_eligible{0},ghost_rejected{0},ghost_drawn{0};
Root query_root=nullptr;
Descriptor query_descriptor=nullptr;
Clone clone=nullptr;
constexpr unsigned hair_count=sizeof(hair_programs)/sizeof(hair_programs[0]);
constexpr unsigned glove_index=hair_count;
constexpr unsigned glove_slot=1;
constexpr unsigned ground_slot=2;
constexpr unsigned ground_first=glove_index+1;
constexpr unsigned ground_count=sizeof(ground_programs)/sizeof(ground_programs[0]);
std::atomic<uint64_t> ground_cloned{0},ground_submitted{0},ground_clone_attempts{0};
std::atomic<uint32_t> ground_clone_status{0};
std::atomic<uint64_t> primary_submitted{0};
std::atomic<uint64_t> ground_clone_mask{0},ground_submit_mask{0};
unsigned ground_clones_by_family[ground_count]={};
constexpr unsigned ground_family_limit=16;
static_assert(ground_count<=64);
std::unique_ptr<unsigned char[]> shaders[ground_first+ground_count];
std::atomic<uint64_t> glove_cloned{0},glove_eligible{0},glove_drawn{0};
std::atomic<uint64_t> cloned{0},eligible{0},drawn{0};
std::mutex state;
thread_local bool internal=false;
uint64_t next_gen=0;

struct Binding {uint32_t param=0,binding=0;};
struct Layout {uint64_t id=0,gen=0;Binding slots[2];bool known[2]={};};
struct Pipeline {uint64_t id=0,gen=0,layout=0,layout_gen=0,clone=0;unsigned slot=0;uint64_t alternate=0;bool preview_only=false,ghost=false;uint32_t index_count=0,alternate_count=0;unsigned normal_bit=0,alternate_bit=0;};
struct Command {api::command_list *id=nullptr;uint64_t pipeline=0,gen=0;bool triangle=false;};
Layout layouts[256];
std::unordered_map<uint64_t,Pipeline> pipelines;
Command commands[512];

template<typename Row,size_t N> Row* find(Row (&rows)[N],uint64_t id) {
    if(id)for(auto& row:rows) if(row.id==id)return &row;
    return nullptr;
}
Pipeline* find(std::unordered_map<uint64_t,Pipeline>& rows,uint64_t id) {
    if (!id) return nullptr;
    const auto it=rows.find(id);
    return it==rows.end()?nullptr:&it->second;
}
Command* command(api::command_list*c,bool create=false) {
    static thread_local Command* recent=nullptr;
    if(!c)return nullptr;
    if(recent&&recent->id==c)return recent;
    Command *free=nullptr;
    for(auto& row:commands) {
        if(row.id==c) return recent=&row;
        if(!row.id&&!free)free=&row;
    }
    if(create&&free){*free={};free->id=c;return recent=free;}
    return nullptr;
}
uint32_t crc(const void*data,size_t n){
    auto p=static_cast<const unsigned char*>(data);
    uint32_t v=~0U;
    while(n--) {v^=*p++;for(unsigned j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320U:0);}
    return ~v;
}
bool initialize(HMODULE core) {
    query_root=reinterpret_cast<Root>(GetProcAddress(core,"ReShadeD3D12QueryGraphicsRootStateV1"));
    query_descriptor=reinterpret_cast<Descriptor>(GetProcAddress(core,"ReShadeD3D12QueryDescriptorMetadataV1"));
    clone=reinterpret_cast<Clone>(GetProcAddress(core,"ReShadeD3D12CloneCurrentPipelinePixelShaderV1"));
    query_ia=reinterpret_cast<IA>(GetProcAddress(core,"ReShadeD3D12QueryIASnapshotV1"));
    if(!query_root||!query_descriptor||!clone)return false;
    for(unsigned i=0;i<ground_first+ground_count;++i) {
        const auto &f=i>=ground_first?ground_programs[i-ground_first].program.candidate:
                      i==glove_index?glove_program.candidate:hair_programs[i].candidate;
        wchar_t path[MAX_PATH]={};
        DWORD n=0;
        if(swprintf_s(path,L"%s\\%s",package_root,f.path)<0||f.bytes>1024*1024)return false;
        shaders[i].reset(new(std::nothrow) unsigned char[f.bytes]);
        if(!shaders[i]||!read_bounded(path,shaders[i].get(),f.bytes,n)||n!=f.bytes||
           !head_upload::CheckSha256(shaders[i].get(),n,f.sha))return false;
    }
    return true;
}

void init_layout(api::device*d,uint32_t n,const api::pipeline_layout_param*params,api::pipeline_layout h) {
    if(!params||n>64||d!=texture_device.load(std::memory_order_acquire))return;
    Layout candidate{h.handle};
    unsigned found[2]={};
    auto inspect=[&](uint32_t param,const api::descriptor_range&r,bool direct){
        if(!(uint32_t(r.visibility)&uint32_t(api::shader_stage::pixel))||r.dx_register_space||
           r.type!=api::descriptor_type::shader_resource_view)return;
        constexpr uint32_t registers[2]={4,glove_register};
        for(unsigned slot=0;slot<2;++slot) {
            const auto reg=registers[slot];
            if(r.dx_register_index>reg||reg-r.dx_register_index>=r.count)continue;
            const auto offset=reg-r.dx_register_index;
            if(direct||r.array_size!=1||r.count>1024||r.binding>UINT32_MAX-offset){
                found[slot]=2;
                continue;
            }
            ++found[slot];
            candidate.slots[slot]={param,r.binding+offset};
        }
    };
    for(uint32_t i=0;i<n;++i) {
        const auto&p=params[i];
        switch(p.type) {
        case api::pipeline_layout_param_type::descriptor_table:
        case api::pipeline_layout_param_type::push_descriptors_with_ranges:
            if(p.descriptor_table.count>128||!p.descriptor_table.ranges)return;
            for(uint32_t j=0;j<p.descriptor_table.count;++j)
                inspect(i,p.descriptor_table.ranges[j],p.type!=api::pipeline_layout_param_type::descriptor_table);
            break;
        case api::pipeline_layout_param_type::descriptor_table_with_flags:
        case api::pipeline_layout_param_type::push_descriptors_with_ranges_and_flags:
            if(p.descriptor_table_with_flags.count>128||!p.descriptor_table_with_flags.ranges)return;
            for(uint32_t j=0;j<p.descriptor_table_with_flags.count;++j)
                inspect(i,p.descriptor_table_with_flags.ranges[j],p.type!=api::pipeline_layout_param_type::descriptor_table_with_flags);
            break;
        case api::pipeline_layout_param_type::push_descriptors: inspect(i,p.push_descriptors,true);break;
        default:break;
        }
    }
    std::lock_guard<std::mutex> guard(state);
    auto old=find(layouts,h.handle);
    if(old)*old={};
    for(unsigned slot=0;slot<2;++slot)candidate.known[slot]=found[slot]==1;
    if(!h.handle||(!candidate.known[0]&&!candidate.known[1]))return;
    for(auto &row:layouts)if(!row.id){candidate.gen=++next_gen;row=candidate;break;}
}
void destroy_layout(api::device*d,api::pipeline_layout h) {
    if(d!=texture_device.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> guard(state);
    if(auto row=find(layouts,h.handle))*row={};
}
void init_pipeline(api::device*d,api::pipeline_layout h,uint32_t n,
                   const api::pipeline_subobject*o,api::pipeline id) {
    if(internal||!id.handle||!o||d!=texture_device.load(std::memory_order_acquire))return;
    const api::shader_desc *vs=nullptr,*ps=nullptr;
    for(uint32_t i=0;i<n;++i)if(o[i].data&&o[i].count==1) {
        const auto kind=o[i].type;
        if(kind==api::pipeline_subobject_type::vertex_shader)vs=static_cast<const api::shader_desc*>(o[i].data);
        if(kind==api::pipeline_subobject_type::pixel_shader)ps=static_cast<const api::shader_desc*>(o[i].data);
        if((kind==api::pipeline_subobject_type::hull_shader||kind==api::pipeline_subobject_type::domain_shader||
            kind==api::pipeline_subobject_type::geometry_shader)&&static_cast<const api::shader_desc*>(o[i].data)->code_size)return;
    }
    if(!vs||!ps||!vs->code||!ps->code)return;
    const ground_asset* beam=nullptr;
    const ground_asset* preview=nullptr;
    unsigned index=0,preview_index=0;
    for(unsigned i=0;i<ground_count;++i) {
        const auto& entry=ground_programs[i];
        if(vs->code_size==entry.vs_bytes && ps->code_size==entry.program.stock_bytes &&
           head_upload::CheckSha256(vs->code,uint32_t(vs->code_size),entry.vs_sha) &&
           head_upload::CheckSha256(ps->code,uint32_t(ps->code_size),entry.program.stock_sha)) {
            if(entry.preview) {
                preview=&entry;
                preview_index=ground_first+i;
            } else {
                beam=&entry;
                index=ground_first+i;
            }
        }
    }
    if(!beam&&preview) {
        beam=preview;
        index=preview_index;
        preview=nullptr;
    }
    const bool glove=!beam&&vs->code_size==glove_vs_size&&ps->code_size==glove_program.stock_bytes;
    const auto slot=beam?ground_slot:glove?glove_slot:0;
    auto &counter=beam?ground_cloned:glove?glove_cloned:cloned;
    const auto capacity=beam?ground_count*ground_family_limit:glove?4u:24u;
    if(counter.load(std::memory_order_acquire)>=capacity)return;
    if(glove)index=glove_index;
    if(!glove&&!beam) {
        if(vs->code_size!=hair_vs_size)return;
        for(index=0;index<hair_count;++index)
            if(ps->code_size==hair_programs[index].stock_bytes)break;
        if(index==hair_count)return;
    }
    const auto &program=beam?beam->program:glove?glove_program:hair_programs[index];
    if(!beam&&(!head_upload::CheckSha256(ps->code,uint32_t(ps->code_size),program.stock_sha)||
       !head_upload::CheckSha256(vs->code,uint32_t(vs->code_size),glove?glove_vs_sha:hair_vs_sha)))return;
    std::lock_guard<std::mutex> guard(state);
    const auto*l=find(layouts,h.handle);
    if((!beam&&(!l||!l->known[slot]))||!h.handle||find(pipelines,id.handle)||counter.load()>=capacity)return;
    const auto family=beam?index-ground_first:0;
    if(beam&&ground_clones_by_family[family]>=ground_family_limit)return;
    {
        Pipeline row;
        rs_pipeline_clone_result_v1 proof={sizeof(proof),1};
        internal=true;
        bool ok=clone(d,id.handle,shaders[index].get(),program.candidate.bytes,&proof);
        internal=false;
        if(beam) {
            ground_clone_attempts.fetch_add(1,std::memory_order_relaxed);
            ground_clone_status.store(proof.status,std::memory_order_relaxed);
        }
        if(ok&&proof.status==rs_clone_created&&proof.clone_pipeline&&proof.non_ps_bytes_equal&&
           proof.original_vs_crc32==(beam?beam->vs_crc:glove?glove_vs_crc:hair_vs_crc)&&proof.original_ps_crc32==program.stock_crc&&
           proof.replacement_ps_crc32==program.candidate_crc){
            row={id.handle,++next_gen,h.handle,l?l->gen:0,proof.clone_pipeline,slot};
            row.preview_only=beam&&beam->preview;
            row.ghost=beam&&beam->vs_crc==0x1DAE341D&&beam->program.stock_crc==0x221DA337;
            row.index_count=beam?beam->index_count:0;
            row.normal_bit=family;
            if(beam){
                ++ground_clones_by_family[family];
                ground_clone_mask.fetch_or(1ULL<<family,std::memory_order_relaxed);
            }
            if(preview) {
                rs_pipeline_clone_result_v1 alternate={sizeof(alternate),1};
                internal=true;
                const bool ready=clone(d,id.handle,shaders[preview_index].get(),preview->program.candidate.bytes,&alternate);
                internal=false;
                if(ready&&alternate.status==rs_clone_created&&alternate.clone_pipeline&&alternate.non_ps_bytes_equal&&
                   alternate.original_vs_crc32==preview->vs_crc&&alternate.original_ps_crc32==preview->program.stock_crc&&
                   alternate.replacement_ps_crc32==preview->program.candidate_crc)
                {
                    row.alternate=alternate.clone_pipeline;
                    row.alternate_count=preview->index_count;
                    row.alternate_bit=preview_index-ground_first;
                    ground_clone_mask.fetch_or(1ULL<<row.alternate_bit,std::memory_order_relaxed);
                }
            }
            pipelines.emplace(id.handle,row);
            counter.fetch_add(1,std::memory_order_release);
        }
    }
}
void destroy_pipeline(api::device*d,api::pipeline id) {
    if((!cloned.load(std::memory_order_acquire)&&!glove_cloned.load(std::memory_order_acquire)&&!ground_cloned.load(std::memory_order_acquire))||d!=texture_device.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> guard(state);
    pipelines.erase(id.handle);
}
void reset(api::command_list*c){
    if(internal)return;
    std::lock_guard<std::mutex> guard(state);
    if(auto row=command(c))*row={};
}
void secondary(api::command_list*c,api::command_list*){reset(c);}
void bind(api::command_list*c,api::pipeline_stage stage,api::pipeline p){
    if(internal||!(uint32_t(stage)&uint32_t(api::pipeline_stage::vertex_shader))||
       c->get_device()!=texture_device.load(std::memory_order_acquire))return;
    std::lock_guard<std::mutex> guard(state);
    if(auto row=command(c,true)){
        const auto*known=find(pipelines,p.handle);
        row->pipeline=known?known->id:0;row->gen=known?known->gen:0;
    }
}
void topology(api::command_list*c,uint32_t n,const api::dynamic_state*states,const uint32_t*vals){
    if(internal||c->get_device()!=texture_device.load(std::memory_order_acquire))return;
    for(uint32_t i=n;i>0;--i)if(states[i-1]==api::dynamic_state::primitive_topology) {
        std::lock_guard<std::mutex> guard(state);
        if(auto row=command(c,true))row->triangle=vals[i-1]==uint32_t(api::primitive_topology::triangle_list);
        return;
    }
}
bool ground_draw(api::command_list* c,uint32_t count,uint32_t instances,uint32_t first,
                 int32_t base,uint32_t first_instance,bool indexed) {
    if(internal||!count||!instances||!ground_cloned.load(std::memory_order_acquire))return false;
    auto*d=texture_device.load(std::memory_order_acquire);
    if(!d||c->get_device()!=d)return false;
    std::lock_guard<std::mutex> guard(state);
    const auto*record=command(c);
    if(!record||!record->triangle||!record->gen)return false;
    const auto*p=find(pipelines,record->pipeline);
    if(!p||p->gen!=record->gen||p->slot!=ground_slot)return false;
    if(!can_replace())return false;
    const bool preview=(p->preview_only||p->alternate)&&primary_ready_for_draw();
    if(p->preview_only&&!preview)return false;
    const bool alternate=preview&&p->alternate;
    const auto needed=alternate?p->alternate_count:p->index_count;
    if(needed&&(!indexed||count!=needed||instances!=1||first_instance))return false;
    rs_graphics_root_snapshot_v1 root={sizeof(root),1};
    if(!query_root(c,0,0,&root)||root.status!=rs_root_complete||!root.recording_generation||
       root.command_list_type!=D3D12_COMMAND_LIST_TYPE_DIRECT||!root.root_signature_known||
       root.root_signature!=p->layout||!can_replace())return false;
    if(p->ghost) {
        ghost_eligible.fetch_add(1,std::memory_order_relaxed);
        rs_ia_snapshot_v1 ia={sizeof(ia),1};
        if(!preview||!indexed||!query_ia||!query_ia(c,3,1,&ia)||
           !ghost_shape::matches(ia,root.recording_generation,count,instances,first,base,first_instance,true)) {
            ghost_rejected.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
    }
    const auto clone_to_bind=alternate?p->alternate:p->clone;
    internal=true;
    c->bind_pipeline(api::pipeline_stage::all_graphics,{clone_to_bind});
    if(indexed)c->draw_indexed(count,instances,first,base,first_instance);
    else c->draw(count,instances,first,first_instance);
    c->bind_pipeline(api::pipeline_stage::all_graphics,{p->id});
    internal=false;
    ground_submit_mask.fetch_or(1ULL<<(alternate?p->alternate_bit:p->normal_bit),std::memory_order_relaxed);
    if(p->ghost)ghost_drawn.fetch_add(1,std::memory_order_relaxed);
    if(preview)primary_submitted.fetch_add(1,std::memory_order_relaxed);
    else ground_submitted.fetch_add(1,std::memory_order_relaxed);
    return true;
}
bool ground_nonindexed(api::command_list* c,uint32_t count,uint32_t instances,uint32_t first,uint32_t first_instance) {
    return ground_draw(c,count,instances,first,0,first_instance,false);
}
bool draw(api::command_list*c,uint32_t n,uint32_t instances,uint32_t first,int32_t base,uint32_t first_instance) {
    if(ground_draw(c,n,instances,first,base,first_instance,true))return true;
    const bool glove=n==glove_indices;
    const auto &counter=glove?glove_cloned:cloned;
    if(internal||!counter.load(std::memory_order_acquire)||
       (!glove&&n!=hair_light::index_count[0]&&n!=hair_light::index_count[1])||
       instances!=1||first||base||first_instance||!can_replace())return false;
    auto*d=texture_device.load(std::memory_order_acquire);
    if(!d||c->get_device()!=d)return false;
    std::lock_guard<std::mutex> guard(state);
    const auto*record=command(c);
    if(!record||!record->triangle||!record->gen)return false;
    const auto*p=find(pipelines,record->pipeline);
    if(!p||p->gen!=record->gen||p->slot!=(glove?glove_slot:0))return false;
    const auto*l=find(layouts,p->layout);
    if(!l||l->gen!=p->layout_gen||!l->known[p->slot])return false;
    const auto &binding=l->slots[p->slot];
    ++(glove?glove_eligible:eligible);
    rs_graphics_root_snapshot_v1 root={sizeof(root),1}, again={sizeof(again),1};
    const auto mask=1ULL<<binding.param;
    if(!query_root(c,mask,1,&root)||root.status!=rs_root_complete||!root.recording_generation||
       root.command_list_type!=D3D12_COMMAND_LIST_TYPE_DIRECT||
       !root.root_signature_known||root.root_signature!=p->layout)return false;
    const auto table=root.parameters[binding.param].value;
    rs_descriptor_metadata_v1 a={sizeof(a),1},b={sizeof(b),1};
    if(!table||!query_descriptor(d,table,binding.binding,0,&a)||
       !hair_light::valid_descriptor(root,binding.param,binding.binding,a))return false;
    const int owner=matcher.ConfirmedFullTexture(a.resource_identity);
    if(glove?owner!=glove_texture_index:!hair_light::draw_shape(owner,n,instances,first,base,first_instance))return false;
    if(!query_descriptor(d,table,binding.binding,0,&b)||!hair_light::same_descriptor(a,b)||
       !query_root(c,mask,1,&again)||std::memcmp(&root,&again,sizeof(root)))return false;
    internal=true;
    c->bind_pipeline(api::pipeline_stage::all_graphics,{p->clone});
    c->draw_indexed(n,instances,first,base,first_instance);
    c->bind_pipeline(api::pipeline_stage::all_graphics,{p->id});
    internal=false;
    ++(glove?glove_drawn:drawn);
    if(glove)
        primary_body_seen.store(GetTickCount64(),std::memory_order_release);
    return true;
}
} // namespace combo_hair
