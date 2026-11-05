#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <d3d12.h>
#include <reshade.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "bc7_upload_v1.h"

namespace head_upload {
namespace api = reshade::api;
using QueryFn = bool(__cdecl *)(void *, rs_bc7_upload_view_v1 *);
using ReplaceFn = bool(__cdecl *)(void *, const rs_bc7_upload_replacement_v1 *);

struct MipContract {
    uint32_t offset, bytes, row_bytes, block_rows;
    const char *stock_sha, *authored_sha;
    bool anchor;
};
struct HeadContract {
    const char *id;
    const wchar_t *stock_path, *authored_path;
    const char *stock_sha, *authored_sha;
    MipContract mips[12];
};
#include "HeadContract.generated.hpp"
#include "StockFingerprint.generated.hpp"
inline constexpr uint32_t stream_bytes = 5592432;

inline bool CheckSha256(const void *bytes, uint32_t length, const char *expected) {
    BCRYPT_ALG_HANDLE sha = nullptr;
    uint8_t hash[32];
    if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    const auto result = BCryptHash(sha, nullptr, 0, const_cast<PUCHAR>(static_cast<const UCHAR *>(bytes)), length, hash, 32);
    BCryptCloseAlgorithmProvider(sha, 0);
    if (result < 0) return false;
    char hex[65];
    static constexpr char chars[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(hash); ++i) {
        hex[2 * i] = chars[hash[i] >> 4];
        hex[2 * i + 1] = chars[hash[i] & 15];
    }
    hex[64] = 0;
    return !strcmp(hex, expected);
}


// Sparse blocks only reject candidates; the complete tightly-packed mip SHA
// establishes identity. Neither row padding nor a complete stock file is kept.
inline bool ValidStockRows(const uint8_t *rows,uint32_t pitch,const MipContract &m) {
    return rows && m.block_rows && m.row_bytes>=16 && !(m.row_bytes%16) &&
        pitch>=m.row_bytes && uint64_t(m.row_bytes)*m.block_rows==m.bytes;
}
inline bool StockQuickMatch(const uint8_t *rows,uint32_t pitch,
                           const uint8_t (&fingerprint)[8][16],const MipContract &m) {
    if (!ValidStockRows(rows,pitch,m)) return false;
    for (unsigned n=0;n<8;++n) {
        const uint32_t row=(m.block_rows-1)*n/7;
        const uint32_t block=(m.row_bytes/16-1)*((n*3)%8)/7;
        if (memcmp(rows+uint64_t(row)*pitch+block*16,fingerprint[n],16)) return false;
    }
    return true;
}
#ifdef HEAD_UPLOAD_STOCKLESS_TESTS
inline uint64_t stockless_full_hash_calls=0;
#endif
inline bool StockEqualRows(const uint8_t *rows,uint32_t pitch,const MipContract &m) {
    if (!ValidStockRows(rows,pitch,m)) return false;
#ifdef HEAD_UPLOAD_STOCKLESS_TESTS
    ++stockless_full_hash_calls;
#endif
    if (pitch==m.row_bytes) return CheckSha256(rows,m.bytes,m.stock_sha);
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    struct Cleanup {
        BCRYPT_ALG_HANDLE &alg;BCRYPT_HASH_HANDLE &hash;
        ~Cleanup(){if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);}
    } cleanup{alg,hash};
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
        BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0) return false;
    for (uint32_t row=0;row<m.block_rows;++row)
        if (BCryptHashData(hash,const_cast<PUCHAR>(rows+uint64_t(row)*pitch),m.row_bytes,0)<0) return false;
    uint8_t digest[32];if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)return false;
    char hex[65];static constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}
    hex[64]=0;return !strcmp(hex,m.stock_sha);
}
inline bool MatchStockUpload(const uint8_t *rows,uint32_t pitch,unsigned part,unsigned mip,
                             const MipContract &m) {
    if(part>=6||mip>=12)return false;
    return StockQuickMatch(rows,pitch,head_stock_fingerprints[part][mip],m) && StockEqualRows(rows,pitch,m);
}

inline std::vector<uint8_t> ReadStream(const std::wstring &path, const char *sha) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    struct Close { HANDLE file; ~Close() { CloseHandle(file); } } close{file};
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart != stream_bytes) return {};
    std::vector<uint8_t> data(stream_bytes);
    DWORD read = 0;
    if (!ReadFile(file, data.data(), stream_bytes, &read, nullptr) || read != stream_bytes ||
        !CheckSha256(data.data(), stream_bytes, sha)) return {};
    return data;
}

inline D3D12_RESOURCE_DESC HeadShape(DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 2048;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 12;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    return desc;
}

struct Metrics {
    uint64_t confirmed[6] = {}, replaced[6] = {}, mismatched[6] = {};
    uint64_t status[8] = {}, early_coarse = 0, unknown_anchor = 0, request_failed = 0, tracking_capacity = 0;
    uint64_t map_reads = 0, max_match_microseconds = 0;
};

class Matcher {
    struct Data {
        api::resource upload = {};
    };
    struct Tracked {
        uint64_t handle = 0;
        int part = -1;
        bool top_confirmed = false;
    };
    std::array<Data, 6> textures;
    std::array<Tracked, 512> tracked = {};
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 12> footprints = {};
    std::mutex lock;
    api::device *owner = nullptr;
    QueryFn query = nullptr;
    ReplaceFn replace = nullptr;
    std::atomic<bool> ready{false};
    bool fixture_checks = false;
    Metrics metrics;

    struct MapRead {
        ID3D12Resource *resource;
        bool mapped = false;
        MapRead(ID3D12Resource *res) : resource(res) {}
        ~MapRead() {
            if (mapped) { const D3D12_RANGE none = {}; resource->Unmap(0, &none); }
        }
    };

public:
    void SetApi(QueryFn q, ReplaceFn r, bool test_wrong_token) {
        query = q;
        replace = r;
        fixture_checks = test_wrong_token;
    }

    bool Initialize(api::device *device, const std::wstring &folder) {
        if (!device || device->get_api() != api::device_api::d3d12 || !query || !replace || owner) return false;
        owner = device;
        auto *native = reinterpret_cast<ID3D12Device *>(device->get_native());
        const auto desc = HeadShape(DXGI_FORMAT_BC7_UNORM_SRGB);
        UINT rows[12] = {};
        UINT64 row_bytes[12] = {}, size = 0;
        native->GetCopyableFootprints(&desc, 0, 12, 0, footprints.data(), rows, row_bytes, &size);
        if (!size || size > 6 * 1024 * 1024) return false;
        for (unsigned part = 0; part < textures.size(); ++part) {
            const auto &contract = head_textures[part];
            auto authored = ReadStream(folder + L"\\" + contract.authored_path, contract.authored_sha);
            if (authored.empty()) return false;
            for (unsigned level = 0; level < 12; ++level) {
                const auto &m = contract.mips[level];
                if (m.row_bytes != row_bytes[level] || m.block_rows != rows[level] || uint64_t(m.offset) + m.bytes > stream_bytes ||
                    uint64_t(m.row_bytes) * m.block_rows != m.bytes ||
                    !CheckSha256(authored.data() + m.offset, m.bytes, m.authored_sha)) return false;
            }
            const api::resource_desc upload_desc(size, api::memory_heap::upload, api::resource_usage::copy_source);
            auto &data = textures[part];
            if (!owner->create_resource(upload_desc, nullptr, api::resource_usage::cpu_access, &data.upload)) return false;
            void *mapped = nullptr;
            const bool mapped_ok = owner->map_buffer_region(data.upload, 0, size, api::map_access::write_only, &mapped);
            if (!mapped_ok || !mapped) {
                if (mapped_ok) owner->unmap_buffer_region(data.upload);
                return false;
            }
            std::memset(mapped, 0x5a, static_cast<size_t>(size));
            for (unsigned level = 0; level < 12; ++level) {
                const auto &m = contract.mips[level];
                auto *dest = static_cast<uint8_t *>(mapped) + footprints[level].Offset;
                for (unsigned row = 0; row < m.block_rows; ++row)
                    memcpy(dest + row * footprints[level].Footprint.RowPitch, authored.data() + m.offset + row * m.row_bytes, m.row_bytes);
            }
            owner->unmap_buffer_region(data.upload);
        }
        ready.store(true, std::memory_order_release);
        return true;
    }

    void OnResource(api::device *device, const api::resource_desc &d, api::resource res) {
        if (!Ready() || device != owner || d.type != api::resource_type::texture_2d || d.texture.width != 2048 ||
            d.texture.height != 2048 || d.texture.levels != 12 || d.texture.depth_or_layers != 1 ||
            (uint32_t(d.texture.format) != 97 && uint32_t(d.texture.format) != 98 && uint32_t(d.texture.format) != 99)) return;
        std::lock_guard<std::mutex> guard(lock);
        for (auto &item : tracked) if (item.handle == res.handle) { item = {res.handle, -1, false}; return; }
        for (auto &item : tracked) if (!item.handle) { item = {res.handle, -1}; return; }
        ++metrics.tracking_capacity;
    }

    void DestroyResource(api::device *device, api::resource res) {
        if (!Ready() || device != owner) return;
        std::lock_guard<std::mutex> guard(lock);
        for (auto &item : tracked) if (item.handle == res.handle) { item = {}; return; }
    }

    bool OnUpload(api::command_list *command, api::resource source, uint64_t offset, api::resource dest, uint32_t mip,
                  bool (*accepting)()) {
        if (!Ready() || command->get_device() != owner) return false;
        std::lock_guard<std::mutex> guard(lock);
        Tracked *tracked_dest = nullptr;
        for (auto &item : tracked) if (item.handle == dest.handle) { tracked_dest = &item; break; }
        if (tracked_dest && mip == 0) tracked_dest->top_confirmed = false;
        if (!tracked_dest || !accepting()) return false;
        rs_bc7_upload_view_v1 v = {sizeof(v), 1};
        if (!query(command, &v) || v.status >= 8) return false;
        ++metrics.status[v.status];
        if (v.status != rs_bc7_upload_eligible || v.subresource != mip || mip >= 12 || v.source_resource != source.handle ||
            v.destination_resource != dest.handle || v.source_offset != offset || !v.token ||
            v.row_pitch != footprints[mip].Footprint.RowPitch || v.row_bytes != head_textures[0].mips[mip].row_bytes ||
            v.block_rows != head_textures[0].mips[mip].block_rows) return false;
        int part = tracked_dest->part;
        if (part < 0 && !head_textures[0].mips[mip].anchor) { ++metrics.early_coarse; return false; }
        LARGE_INTEGER start, end, frequency;
        QueryPerformanceCounter(&start);
        QueryPerformanceFrequency(&frequency);
        const auto clock = [&]() {
            QueryPerformanceCounter(&end);
            if (frequency.QuadPart <= 0 || end.QuadPart < start.QuadPart) return;
            const auto elapsed = uint64_t(end.QuadPart - start.QuadPart) * 1000000ull / uint64_t(frequency.QuadPart);
            if (elapsed > metrics.max_match_microseconds) metrics.max_match_microseconds = elapsed;
        };
        auto *native_source = reinterpret_cast<ID3D12Resource *>(source.handle);
        MapRead pin(native_source);
        const D3D12_RANGE read = {static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + v.source_span)};
        void *mapped = nullptr;
        pin.mapped = SUCCEEDED(native_source->Map(0, &read, &mapped));
        if (!pin.mapped || !mapped) { clock(); return false; }
        ++metrics.map_reads;
        const uint8_t *bytes = static_cast<const uint8_t *>(mapped) + offset;
        bool verified_here = false;
        if (part < 0) {
            for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
                const auto &m = head_textures[i].mips[mip];
                if (!m.anchor || !MatchStockUpload(bytes, v.row_pitch, unsigned(i), mip, m)) continue;
                if (part >= 0) { ++metrics.unknown_anchor; clock(); return false; }
                part = i;
                verified_here = true;
            }
            if (part < 0) { ++metrics.unknown_anchor; clock(); return false; }
        }
        const auto &m = head_textures[part].mips[mip];
        if (!verified_here && !MatchStockUpload(bytes, v.row_pitch, unsigned(part), mip, m)) {
            ++metrics.mismatched[part]; clock(); return false;
        }
        rs_bc7_upload_replacement_v1 replacement = {sizeof(replacement), 1, v.token, textures[part].upload.handle, footprints[mip].Offset};
        if (fixture_checks) {
            auto stale = replacement;
            ++stale.token;
            if (replace(command, &stale)) { ++metrics.request_failed; clock(); return false; }
        }
        if (!replace(command, &replacement)) { ++metrics.request_failed; clock(); return false; }
        if (tracked_dest->part < 0) {
            tracked_dest->part = part;
            ++metrics.confirmed[part];
        }
        if (mip == 0 && part >= 0 && part < int(textures.size())) tracked_dest->top_confirmed = true;
        ++metrics.replaced[part];
        clock();
        return false; // The native hook records the same copy with our source.
    }

    int ConfirmedFullTexture(uint64_t handle) {
        if (!handle || !Ready()) return -1;
        std::unique_lock<std::mutex> guard(lock,std::try_to_lock);
        if (!guard.owns_lock()) return -1;
        for (const auto &item:tracked)
            if (item.handle==handle && item.top_confirmed && item.part>=0) return item.part;
        return -1;
    }

    Metrics Snapshot() {
        std::lock_guard<std::mutex> guard(lock);
        return metrics;
    }
    bool Ready() const { return ready.load(std::memory_order_acquire); }
};
} // namespace head_upload
