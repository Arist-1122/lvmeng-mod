#pragma once

#include <windows.h>
#include <cstddef>
#include <cstring>
#include <cwchar>

namespace status_file {
enum class result { published, retry, failed };

namespace detail {
// FileRenameInfoEx and its flags are hidden by the SDK when compiling for
// baseline Windows 10. The ABI remains available on newer Windows builds.
constexpr auto rename_info_ex = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);
constexpr DWORD replace_if_exists = 1;
constexpr DWORD posix_semantics = 2;
struct rename_payload {
    DWORD flags;
    HANDLE root_directory;
    DWORD name_bytes;
    wchar_t filename[MAX_PATH];
};
static_assert(offsetof(rename_payload, filename) == offsetof(FILE_RENAME_INFO, FileName));

inline bool sharing_error(DWORD error) {
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
}

inline result rename_error(DWORD error, const wchar_t *path) {
    if (sharing_error(error)) return result::retry;
    // Legacy MoveFileEx may report ACCESS_DENIED for a reader of a regular,
    // writable target. Keep the last complete snapshot and retry next update.
    if (error == ERROR_ACCESS_DENIED) {
        const DWORD attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))) return result::retry;
    }
    return result::failed;
}

inline result publish(const wchar_t *path, const wchar_t *temporary, const char *text,
                      DWORD size, bool prefer_extended) {
    const size_t length = std::wcslen(path);
    if (!length || length >= MAX_PATH || !size) return result::failed;
    constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE | (prefer_extended ? DELETE : 0), share,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return sharing_error(error) ? result::retry : result::failed;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text, size, &written, nullptr);
    const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok || written != size) {
        CloseHandle(file);
        return sharing_error(write_error) ? result::retry : result::failed;
    }
    if (prefer_extended) {
        rename_payload payload = {};
        payload.flags = replace_if_exists | posix_semantics;
        payload.name_bytes = static_cast<DWORD>(length * sizeof(wchar_t));
        std::memcpy(payload.filename, path, payload.name_bytes);
        const DWORD bytes = static_cast<DWORD>(offsetof(rename_payload, filename)) + payload.name_bytes;
        const BOOL renamed = SetFileInformationByHandle(file, rename_info_ex, &payload, bytes);
        const DWORD error = renamed ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (renamed) return result::published;
        if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED &&
            error != ERROR_INVALID_FUNCTION && error != ERROR_CALL_NOT_IMPLEMENTED) return rename_error(error, path);
    } else {
        CloseHandle(file);
    }
    if (MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING)) return result::published;
    return rename_error(GetLastError(), path);
}
} // namespace detail

inline result publish(const wchar_t *path, const wchar_t *temporary, const char *text, DWORD size) {
    return detail::publish(path, temporary, text, size, true);
}
} // namespace status_file
