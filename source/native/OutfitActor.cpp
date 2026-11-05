#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <reshade_events.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "HeadUploadMatcher.hpp"
#include "SessionIdentity.hpp"
#include "StatusFile.hpp"
#include "OutfitContract.hpp"

namespace {
namespace api = reshade::api;
using RegisterAddon = bool (*)(void *, uint32_t);
using UnregisterAddon = void (*)(void *);
using RegisterEvent = void (*)(reshade::addon_event, void *);

game_runtime::session_identity identity;
head_upload::Matcher matcher;
std::atomic<api::device *> texture_device{nullptr};
std::atomic<bool> device_failed{false};
std::atomic<uint64_t> active_until{0};
HANDLE owner = nullptr, stop = nullptr;
UnregisterAddon unregister_addon = nullptr;
LONG initialization_attempted = 0;
bool registered = false;

bool read_bounded(const wchar_t *path, void *buffer, DWORD capacity, DWORD &count) {
    const auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    count = 0;
    const bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= capacity &&
        ReadFile(file, buffer, DWORD(size.QuadPart), &count, nullptr) && count == size.QuadPart;
    CloseHandle(file);
    return ok;
}

bool process_start(HANDLE process, char (&formatted)[40], uint64_t *dotnet_ticks = nullptr) {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return false;
    SYSTEMTIME time = {};
    if (!FileTimeToSystemTime(&created, &time)) return false;
    ULARGE_INTEGER ticks = {};
    ticks.LowPart = created.dwLowDateTime;
    ticks.HighPart = created.dwHighDateTime;
    if (dotnet_ticks) *dotnet_ticks = ticks.QuadPart + 504911232000000000ULL;
    return snprintf(formatted, sizeof(formatted), "%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ",
                    time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,
                    ticks.QuadPart % 10000000ULL) == 28;
}

int64_t wall_millis() {
    FILETIME value;
    GetSystemTimeAsFileTime(&value);
    ULARGE_INTEGER ticks = {};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return int64_t(ticks.QuadPart/10000) - 11644473600000LL;
}

bool owner_alive() {
    return owner && WaitForSingleObject(owner, 0) == WAIT_TIMEOUT;
}

bool can_replace() {
    return !device_failed.load(std::memory_order_acquire) &&
           GetTickCount64() < active_until.load(std::memory_order_acquire) && owner_alive();
}

#include "PreviewRuntime.inl"
std::atomic<bool> primary_preview_waiting{false};
#include "ComboHair.inl"
#include "SurfaceLightRuntime.inl"
bool surface_combined_draw(api::command_list*c,uint32_t n,uint32_t instances,uint32_t first,int32_t base,uint32_t first_instance) {
    if(surface_light::draw(c,n,instances,first,base,first_instance))return true;
    return combo_hair::draw(c,n,instances,first,base,first_instance);
}

void on_device(api::device *device) {
    if (device->get_api() != api::device_api::d3d12) return;
    api::device *expected = nullptr;
    if (!texture_device.compare_exchange_strong(expected, device)) return;
    try {
        if (matcher.Initialize(device, std::wstring(package_root)+L"\\payload\\outfit")) return;
    } catch (...) {
    }
    device_failed.store(true, std::memory_order_release);
    active_until.store(0, std::memory_order_release);
}

void on_device_destroy(api::device *device) {
    if (device != texture_device.load(std::memory_order_acquire)) return;
    device_failed.store(true, std::memory_order_release);
    active_until.store(0, std::memory_order_release);
}

void on_resource(api::device *device, const api::resource_desc &desc, const api::subresource_data *,
                 api::resource_usage, api::resource resource) {
    matcher.OnResource(device, desc, resource);
}

void on_resource_destroy(api::device *device, api::resource resource) {
    matcher.DestroyResource(device, resource);
}

bool on_copy(api::command_list *commands, api::resource src, uint64_t offset, uint32_t, uint32_t,
             api::resource dest, uint32_t mip, const api::subresource_box *) {
    if (!can_replace()) return false;
    return matcher.OnUpload(commands, src, offset, dest, mip, can_replace);
}

status_file::result write_status(const mesh_trial::runtime_lease &lease, bool stopped, bool status_failed) {
    const bool failed = status_failed || device_failed.load(std::memory_order_acquire);
    const bool active = !failed && !stopped && can_replace();
    const auto metrics = matcher.Snapshot();
    const char *reason = failed ? "upload_runtime_failed" : stopped ? "future_uploads_stopped" :
        !matcher.Ready() ? "awaiting_device" : metrics.replaced[3] && metrics.replaced[4] ?
        "both_outfit_atlases_matched" : "awaiting_outfit_uploads";
    char text[2048] = {};
    const int count = snprintf(text, sizeof(text),
        "{\"version\":1,\"pid\":%lu,\"targetStartUtc\":\"%s\",\"sessionId\":\"%s\",\"contractSha256\":\"%s\","
        "\"commandId\":%lld,\"mode\":\"%s\",\"rejectReason\":\"%s\",\"trackingFailed\":%s,"
        "\"replacedDraws\":0,\"outfitBody01Mips\":%llu,\"outfitBody02Mips\":%llu,\"weaponMips\":%llu,"
        "\"outfitAlreadyUploaded\":%s,\"outfitUploadReady\":%s,\"hairSilverMips\":%llu,\"hairLightClones\":%llu,\"hairLightEligibleDraws\":%llu,\"hairLightDraws\":%llu,\"gloveLightDraws\":%llu,\"combatVfxEnabled\":true,\"groundClones\":%llu,\"groundGuardSubmissions\":%llu,\"primaryPreviewSubmissions\":%llu,\"primaryPreviewActive\":%s,\"primaryPreviewWaiting\":%s,\"combatCloneAttempts\":%llu,\"combatLastCloneStatus\":%u,\"ghostEligibleDraws\":%llu,\"ghostIaRejected\":%llu,\"ghostDraws\":%llu,\"surfaceLightClones\":%llu,\"surfaceLightDraws\":%llu,\"surfaceLightOwnerRejected\":%llu,\"surfaceLightOwnerDraws\":[%llu,%llu,%llu,%llu,%llu,%llu],\"combatCloneMask\":\"%016llx\",\"combatSubmitMask\":\"%016llx\"}",
        GetCurrentProcessId(),identity.lease_identity.target_start_utc,identity.lease_identity.session,
        runtime_contract_sha,lease.acknowledged_command(),active ? "replace" : "original",reason,failed ? "true" : "false",
        metrics.replaced[3],metrics.replaced[4],metrics.replaced[5],
        (metrics.replaced[0]||metrics.replaced[1]||metrics.replaced[2]||metrics.replaced[3]||metrics.replaced[4]||metrics.replaced[5]) ? "true" : "false",
        matcher.Ready() ? "true" : "false", metrics.replaced[0]+metrics.replaced[1],
        combo_hair::cloned.load(),combo_hair::eligible.load(),combo_hair::drawn.load(),combo_hair::glove_drawn.load(),combo_hair::ground_cloned.load(),combo_hair::ground_submitted.load(),
        combo_hair::primary_submitted.load(),primary_preview_until.load()?"true":"false",primary_preview_waiting.load()?"true":"false",
        combo_hair::ground_clone_attempts.load(),combo_hair::ground_clone_status.load(),
        combo_hair::ghost_eligible.load(),combo_hair::ghost_rejected.load(),combo_hair::ghost_drawn.load(),
        surface_light::cloned.load(),surface_light::drawn.load(),surface_light::owner_rejected.load(),
        surface_light::owner_draws[0].load(),surface_light::owner_draws[1].load(),surface_light::owner_draws[2].load(),
        surface_light::owner_draws[3].load(),surface_light::owner_draws[4].load(),surface_light::owner_draws[5].load(),
        combo_hair::ground_clone_mask.load(),combo_hair::ground_submit_mask.load());
    if (count <= 0 || count >= int(sizeof(text))) return status_file::result::failed;
    wchar_t path[MAX_PATH] = {}, temp[MAX_PATH] = {};
    if (swprintf_s(path,L"%s\\control\\status_%lu.json",package_root,GetCurrentProcessId()) < 0 ||
        swprintf_s(temp,L"%s.tmp",path) < 0) return status_file::result::failed;
    return status_file::publish(path,temp,text,DWORD(count));
}

DWORD WINAPI control_loop(void *) {
    mesh_trial::runtime_lease lease(identity.lease_identity);
    const uint64_t bootstrap_deadline = active_until.load(std::memory_order_acquire);
    uint64_t last_status = 0;
    unsigned sharing_retries = 0;
    bool stopped = false, status_failed = false;
    wchar_t request_path[MAX_PATH] = {};
    wchar_t cancel_path[MAX_PATH] = {};
    wchar_t preview_path[MAX_PATH] = {};
    wchar_t shared_path[MAX_PATH] = {};
    beam_preview::Window preview;
    if (swprintf_s(request_path,L"%s\\control\\request.json",package_root) < 0 ||
        swprintf_s(cancel_path,L"%s\\control\\cancel_%hs.json",package_root,identity.lease_identity.session) < 0 ||
        swprintf_s(preview_path,L"%s\\control\\beam_%hs.json",package_root,identity.lease_identity.session) < 0 ||
        swprintf_s(shared_path,L"%s\\control\\shared_%hs.json",package_root,identity.lease_identity.session) < 0) {
        active_until.store(0, std::memory_order_release);
        return 1;
    }
    do {
        const uint64_t tick = GetTickCount64();
        if (!owner_alive() || GetFileAttributesW(cancel_path) != INVALID_FILE_ATTRIBUTES) stopped = true;
        char json[4096];
        DWORD length = 0;
        mesh_trial::request command;
        if (!stopped && !status_failed &&
            read_bounded(request_path,json,sizeof(json),length) && mesh_trial::request_parser(json,length).parse(command)) {
            const bool matched = lease.apply(command,wall_millis(),tick);
            if (matched && !command.replace) stopped = true;
        } else if (lease.acknowledged_command() != 0) {
            lease.reject("invalid_or_missing_request");
        }
        uint64_t next = 0;
        if (!stopped && !status_failed && !device_failed.load(std::memory_order_acquire)) {
            if (lease.active(wall_millis(),tick)) next = lease.monotonic_deadline();
            else if (lease.acknowledged_command() == 0 && tick < bootstrap_deadline) next = bootstrap_deadline;
        }
        active_until.store(next,std::memory_order_release);
        const auto now=GetTickCount64();
        const bool active=next&&lease.acknowledged_command()!=0;
        const bool enabled=active&&read_bounded(shared_path,json,sizeof(json),length)&&length==4&&
            json[0]==34&&json[1]=='o'&&json[2]=='n'&&json[3]==34;
        const bool visible=enabled&&primary_focused()&&primary_recent_body(now);
        primary_preview_interrupted.exchange(false,std::memory_order_acq_rel);
        primary_preview_until.store(visible?now+1000:0,std::memory_order_release);
        primary_preview_waiting.store(enabled&&!visible,std::memory_order_release);
        if (!last_status || tick-last_status >= 1000) {
            const auto publication = write_status(lease,stopped,status_failed);
            if (publication == status_file::result::published) sharing_retries = 0;
            else if (publication == status_file::result::failed || ++sharing_retries >= 3) {
                status_failed = true;
                active_until.store(0,std::memory_order_release);
            }
            last_status = tick;
        }
        if (!owner_alive()) break;
    } while (WaitForSingleObject(stop,250) == WAIT_TIMEOUT);
    primary_preview_until.store(0,std::memory_order_release);
    active_until.store(0,std::memory_order_release);
    return 0;
}

template<reshade::addon_event Event>
void register_one(RegisterEvent function, typename reshade::addon_event_traits<Event>::decl callback) {
    function(Event,reinterpret_cast<void *>(callback));
}
} // namespace

extern "C" __declspec(dllexport) const char *NAME = "Lvmeng original outfit and silver hair";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Five exact BC7 identities and bounded hair-only pixel passes";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon, HMODULE core) {
    if (InterlockedCompareExchange(&initialization_attempted,1,0) != 0) return false;
    if (!portable_runtime::initialize(addon)) return false;
    wchar_t path[MAX_PATH] = {};
    DWORD size = GetModuleFileNameW(nullptr,path,MAX_PATH);
    if (!size || size >= MAX_PATH || _wcsicmp(path,expected_host) != 0) return false;
    size = GetModuleFileNameW(core,path,MAX_PATH);
    if (!size || size >= MAX_PATH || !portable_runtime::exact_core_path(core)) return false;
    constexpr DWORD max_core = 16*1024*1024;
    std::unique_ptr<unsigned char[]> core_bytes(new(std::nothrow) unsigned char[max_core]);
    DWORD core_count = 0;
    if (!core_bytes || !read_bounded(path,core_bytes.get(),max_core,core_count) ||
        !head_upload::CheckSha256(core_bytes.get(),core_count,required_core_sha)) return false;
    core_bytes.reset();
    char contract_bytes[262144]; DWORD contract_count = 0;
    if (swprintf_s(path,L"%s\\runtime_contract.json",package_root) < 0 ||
        !read_bounded(path,contract_bytes,sizeof(contract_bytes),contract_count) ||
        !head_upload::CheckSha256(contract_bytes,contract_count,runtime_contract_sha)) return false;

    char json[4096], start[40];
    DWORD length = 0;
    if (swprintf_s(path,L"%s\\control\\session_%lu.json",package_root,GetCurrentProcessId()) < 0 ||
        !read_bounded(path,json,sizeof(json),length) ||
        !game_runtime::session_parser(json,length).parse(identity) ||
        identity.lease_identity.target_pid != GetCurrentProcessId() ||
        strcmp(identity.lease_identity.contract,runtime_contract_sha) != 0 ||
        !process_start(GetCurrentProcess(),start) ||
        strcmp(start,identity.lease_identity.target_start_utc) != 0) return false;
    owner = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,identity.owner_pid);
    uint64_t owner_ticks = 0;
    if (!owner_alive() || !process_start(owner,start,&owner_ticks) || owner_ticks != identity.owner_start_ticks) {
        if (owner) CloseHandle(owner);
        owner = nullptr;
        return false;
    }

    if(!combo_hair::initialize(core)||!surface_light::initialize(core)){
        CloseHandle(owner);
        owner=nullptr;
        return false;
    }
    const auto query = reinterpret_cast<head_upload::QueryFn>(GetProcAddress(core,"ReShadeD3D12QueryCanonicalBC7UploadV1"));
    const auto replace = reinterpret_cast<head_upload::ReplaceFn>(GetProcAddress(core,"ReShadeD3D12ReplaceCanonicalBC7UploadV1"));
    const auto register_addon = reinterpret_cast<RegisterAddon>(GetProcAddress(core,"ReShadeRegisterAddon"));
    unregister_addon = reinterpret_cast<UnregisterAddon>(GetProcAddress(core,"ReShadeUnregisterAddon"));
    const auto register_event = reinterpret_cast<RegisterEvent>(GetProcAddress(core,"ReShadeRegisterEvent"));
    stop = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!stop || !query || !replace || !register_addon || !unregister_addon || !register_event || !register_addon(addon,20)) {
        if (stop) CloseHandle(stop);
        CloseHandle(owner);
        owner = stop = nullptr;
        return false;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&initialization_attempted),&pinned)) {
        unregister_addon(addon);
        CloseHandle(owner);
        CloseHandle(stop);
        owner = stop = nullptr;
        return false;
    }
    matcher.SetApi(query,replace,false);
    active_until.store(GetTickCount64()+45000,std::memory_order_release);
    const HANDLE worker = CreateThread(nullptr,0,control_loop,nullptr,0,nullptr);
    if (!worker) {
        active_until.store(0,std::memory_order_release);
        unregister_addon(addon);
        CloseHandle(owner);
        CloseHandle(stop);
        owner = stop = nullptr;
        return false;
    }
    CloseHandle(worker);
    register_one<reshade::addon_event::init_device>(register_event,on_device);
    register_one<reshade::addon_event::destroy_device>(register_event,on_device_destroy);
    register_one<reshade::addon_event::destroy_device>(register_event,surface_light::destroy_device);
    register_one<reshade::addon_event::init_resource>(register_event,on_resource);
    register_one<reshade::addon_event::destroy_resource>(register_event,on_resource_destroy);
    register_one<reshade::addon_event::copy_buffer_to_texture>(register_event,on_copy);
    register_one<reshade::addon_event::init_pipeline_layout>(register_event,combo_hair::init_layout);
    register_one<reshade::addon_event::init_pipeline_layout>(register_event,surface_light::init_layout);
    register_one<reshade::addon_event::destroy_pipeline_layout>(register_event,combo_hair::destroy_layout);
    register_one<reshade::addon_event::destroy_pipeline_layout>(register_event,surface_light::destroy_layout);
    register_one<reshade::addon_event::init_pipeline>(register_event,combo_hair::init_pipeline);
    register_one<reshade::addon_event::init_pipeline>(register_event,surface_light::init_pipeline);
    register_one<reshade::addon_event::destroy_pipeline>(register_event,combo_hair::destroy_pipeline);
    register_one<reshade::addon_event::destroy_pipeline>(register_event,surface_light::destroy_pipeline);
    register_one<reshade::addon_event::reset_command_list>(register_event,combo_hair::reset);
    register_one<reshade::addon_event::reset_command_list>(register_event,surface_light::reset);
    register_one<reshade::addon_event::destroy_command_list>(register_event,combo_hair::reset);
    register_one<reshade::addon_event::destroy_command_list>(register_event,surface_light::reset);
    register_one<reshade::addon_event::execute_secondary_command_list>(register_event,combo_hair::secondary);
    register_one<reshade::addon_event::execute_secondary_command_list>(register_event,surface_light::secondary);
    register_one<reshade::addon_event::bind_pipeline>(register_event,combo_hair::bind);
    register_one<reshade::addon_event::bind_pipeline>(register_event,surface_light::bind);
    register_one<reshade::addon_event::bind_pipeline_states>(register_event,combo_hair::topology);
    register_one<reshade::addon_event::bind_pipeline_states>(register_event,surface_light::topology);
    register_one<reshade::addon_event::draw_indexed>(register_event,surface_combined_draw);
    register_one<reshade::addon_event::draw>(register_event,combo_hair::ground_nonindexed);
    register_one<reshade::addon_event::init_swapchain>(register_event,primary_swapchain_created);
    register_one<reshade::addon_event::destroy_swapchain>(register_event,primary_swapchain_destroyed);
    registered = true;
    return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE) {
    if (!registered) return;
    device_failed.store(true,std::memory_order_release);
    active_until.store(0,std::memory_order_release);
    SetEvent(stop);
    unregister_addon(addon);
    registered = false;
}

extern "C" __declspec(dllexport) bool PortableRuntimePreflight(HMODULE addon,wchar_t* root,DWORD root_capacity,wchar_t* host,DWORD host_capacity) {
    if(!root||!host||root_capacity<MAX_PATH||host_capacity<MAX_PATH||!portable_runtime::initialize(addon))return false;
    wchar_t path[MAX_PATH]={};std::unique_ptr<unsigned char[]> bytes(new(std::nothrow) unsigned char[16*1024*1024]);DWORD count=0;
    if(!bytes||swprintf_s(path,L"%s\\core\\dxgi.dll",package_root)<0||!read_bounded(path,bytes.get(),16*1024*1024,count)||!head_upload::CheckSha256(bytes.get(),count,required_core_sha))return false;
    if(swprintf_s(path,L"%s\\runtime_contract.json",package_root)<0||!read_bounded(path,bytes.get(),262144,count)||!head_upload::CheckSha256(bytes.get(),count,runtime_contract_sha))return false;
    return !wcscpy_s(root,root_capacity,package_root)&&!wcscpy_s(host,host_capacity,expected_host);
}

BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID) { return TRUE; }
