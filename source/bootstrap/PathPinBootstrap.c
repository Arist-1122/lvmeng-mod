#define _WIN32_WINNT 0x0601
#include <windows.h>
static HMODULE self_module;

static DWORD core_directory(WCHAR *directory,DWORD capacity) {
    DWORD n=GetModuleFileNameW(self_module,directory,capacity),i;
    if(!n||n>=capacity)return ERROR_INSUFFICIENT_BUFFER;
    for(i=n;i>0&&directory[i-1]!=L'\\';--i){}
    if(!i)return ERROR_BAD_PATHNAME;directory[i-1]=0;return ERROR_SUCCESS;
}
// Read-only test hook: resolves this DLL's core directory, never loads graphics.
DWORD WINAPI ResolveCoreDirectory(WCHAR *directory,DWORD capacity) {return core_directory(directory,capacity);}

DWORD WINAPI StartReShade(LPVOID unused) {
    WCHAR previous[32768],directory[MAX_PATH],dll[MAX_PATH],logpath[MAX_PATH];
    DWORD prior_count,prior_error,error,restore_error=0,written;HMODULE core;HANDLE file;char data[1024];
    error=core_directory(directory,MAX_PATH);if(error)return error;
    if(lstrlenW(directory)+10>=MAX_PATH)return ERROR_INSUFFICIENT_BUFFER;
    lstrcpyW(dll,directory);lstrcatW(dll,L"\\dxgi.dll");
    SetLastError(ERROR_SUCCESS);prior_count=GetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",previous,32768);prior_error=GetLastError();
    if(prior_count>=32768)return ERROR_INSUFFICIENT_BUFFER;
    if(!prior_count&&prior_error!=ERROR_SUCCESS&&prior_error!=ERROR_ENVVAR_NOT_FOUND)return prior_error;
    if(!SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",directory))return GetLastError();
    core=LoadLibraryW(dll);error=core?0:GetLastError();
    if(!SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",prior_count?previous:(prior_error==ERROR_ENVVAR_NOT_FOUND?0:L"")))restore_error=GetLastError();
    if(lstrlenW(directory)+45>=MAX_PATH)return ERROR_INSUFFICIENT_BUFFER;
    wsprintfW(logpath,L"%s\\..\\logs\\bootstrap_%lu.json",directory,GetCurrentProcessId());
    wsprintfA(data,"{\"pid\":%lu,\"coreLoadSucceeded\":%d,\"coreLoadWin32Error\":%lu,\"basePathPinnedDuringLoad\":true,\"previousBasePathRestored\":%d,\"restoreWin32Error\":%lu}",GetCurrentProcessId(),core!=0,error,restore_error==0,restore_error);
    file=CreateFileW(logpath,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE)return GetLastError();
    if(!WriteFile(file,data,lstrlenA(data),&written,0)){DWORD e=GetLastError();CloseHandle(file);return e;}
    if(written!=(DWORD)lstrlenA(data)){CloseHandle(file);return ERROR_WRITE_FAULT;}CloseHandle(file);return error?error:restore_error;
}
BOOL WINAPI LibMain(HINSTANCE instance,DWORD reason,LPVOID reserved){if(reason==DLL_PROCESS_ATTACH)self_module=instance;return TRUE;}
