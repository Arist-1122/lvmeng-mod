#pragma once
#include <windows.h>
#include <string>
#include <cwchar>
#include <cstring>

// Immutable after AddonInit. All existing upload/shader/control code consumes
// these buffers; no current-directory or process-global environment dependency.
inline wchar_t package_root[MAX_PATH] = {};
inline wchar_t expected_host[MAX_PATH] = {};

namespace portable_runtime {
inline bool read_small(const wchar_t* path, char* output, DWORD capacity, DWORD& length) {
    length = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= capacity && ReadFile(file, output, DWORD(size.QuadPart), &length, nullptr) && length == size.QuadPart;
    CloseHandle(file); return ok;
}

// Four-field JSON from GameLocation.cs. Reject duplicate/unknown fields,
// malformed UTF-8, unpaired surrogates, embedded NUL and unbounded paths.
class location_parser {
    const char *p, *end;
    void ws() { while (p < end && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')) ++p; }
    bool take(char c) { ws(); if (p == end || *p != c) return false; ++p; return true; }
    bool hex4(unsigned& n) {
        n = 0;
        for (unsigned i = 0; i < 4; ++i) { if (p == end) return false; unsigned char c = *p++; unsigned v; if(c >= '0' && c <= '9') v=c-'0'; else if(c >= 'a' && c <= 'f') v=c-'a'+10; else if(c >= 'A' && c <= 'F') v=c-'A'+10; else return false; n=n*16+v; } return true;
    }
    static void utf8(std::string& out, unsigned n) {
        if(n<0x80) out += char(n);
        else if(n<0x800) {out += char(0xc0|(n>>6));out += char(0x80|(n&63));}
        else if(n<0x10000) {out += char(0xe0|(n>>12));out += char(0x80|((n>>6)&63));out += char(0x80|(n&63));}
        else {out += char(0xf0|(n>>18));out += char(0x80|((n>>12)&63));out += char(0x80|((n>>6)&63));out += char(0x80|(n&63));}
    }
    bool str(std::wstring& out) {
        if (!take('"')) return false; std::string bytes;
        while(p < end && *p != '"') {
            unsigned char c=*p++; if(c<32) return false;
            if(c=='\\') {
                if(p==end) return false; c=*p++;
                if(c=='"'||c=='\\'||c=='/') bytes+=char(c);
                else if(c=='u') { unsigned n=0; if(!hex4(n)||n<32) return false;
                    if(n>=0xd800&&n<=0xdbff) {if(end-p<2||*p++!='\\'||*p++!='u') return false;unsigned low=0;if(!hex4(low)||low<0xdc00||low>0xdfff)return false;n=0x10000+((n-0xd800)<<10)+(low-0xdc00);}
                    else if(n>=0xdc00&&n<=0xdfff) return false;
                    utf8(bytes,n);
                } else return false;
            } else bytes+=char(c);
            if(bytes.size()>8192) return false;
        }
        if(p==end) return false; ++p;
        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);
        if(n<0||(!bytes.empty()&&n==0)||n>=MAX_PATH) return false;
        out.assign(size_t(n),L'\0');
        return !n || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),&out[0],n)==n;
    }
public:
    location_parser(const char* data, size_t size):p(data),end(data+size) { if(size>=3 && (unsigned char)p[0]==0xef && (unsigned char)p[1]==0xbb && (unsigned char)p[2]==0xbf) p+=3; }
    bool parse(std::wstring& host) {
        if(end-p>16384||!take('{'))return false;unsigned seen=0;std::wstring mode,uri;
        for(unsigned i=0;i<4;++i) {std::wstring key,value;unsigned bit=0;if(!str(key)||!take(':'))return false;
            if(key==L"version") {bit=1;if(!take('1'))return false;}
            else if(key==L"gameExecutable") {bit=2;if(!str(host))return false;}
            else if(key==L"launchMode") {bit=4;if(!str(mode))return false;}
            else if(key==L"launchUri") {bit=8;if(!str(uri))return false;}
            else return false;
            if(seen&bit)return false;seen|=bit;if(i!=3&&!take(','))return false;
        }
        if(seen!=15||!take('}'))return false;ws();if(p!=end)return false;
        if(mode!=L"taptap"&&mode!=L"direct"&&mode!=L"manual")return false;
        if(mode==L"taptap"&&uri!=L"taptap://taptap.com/app?app_id=237561&auto_launch=true&platform=pc")return false;
        return !host.empty();
    }
};

inline bool load_location(const wchar_t* root, wchar_t (&host)[MAX_PATH]) {
    wchar_t path[MAX_PATH]={};char json[16384];DWORD count=0;
    if(swprintf_s(path,L"%s\\game-location.json",root)<0||!read_small(path,json,sizeof(json),count))return false;
    std::wstring value;if(!location_parser(json,count).parse(value))return false;
    if(value.size()<4 || !((value[1]==L':'&&(value[2]==L'\\'||value[2]==L'/'))||(value[0]==L'\\'&&value[1]==L'\\')))return false;
    DWORD n=GetFullPathNameW(value.c_str(),MAX_PATH,host,nullptr);if(!n||n>=MAX_PATH)return false;
    const wchar_t* file=wcsrchr(host,L'\\');
    DWORD attributes=GetFileAttributesW(host);
    return file && !_wcsicmp(file+1,L"Lumin.exe") && attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}

inline bool root_from_module_path(const wchar_t* module, wchar_t (&root)[MAX_PATH]) {
    if(!module||wcslen(module)>=MAX_PATH)return false;
    wcscpy_s(root,module);auto slash=wcsrchr(root,L'\\');if(!slash)return false;*slash=0;
    slash=wcsrchr(root,L'\\');if(!slash||_wcsicmp(slash+1,L"addons"))return false;*slash=0;
    wchar_t manifest[MAX_PATH]={};if(swprintf_s(manifest,L"%s\\package.json",root)<0)return false;
    const DWORD attributes=GetFileAttributesW(manifest);
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
}

inline bool initialize(HMODULE addon) {
    wchar_t module[MAX_PATH]={};DWORD n=GetModuleFileNameW(addon,module,MAX_PATH);
    return n&&n<MAX_PATH&&root_from_module_path(module,package_root)&&load_location(package_root,expected_host);
}
inline bool exact_core_path(HMODULE core) {
    wchar_t actual[MAX_PATH]={},wanted[MAX_PATH]={};DWORD n=GetModuleFileNameW(core,actual,MAX_PATH);
    return n&&n<MAX_PATH&&swprintf_s(wanted,L"%s\\core\\dxgi.dll",package_root)>0&&!_wcsicmp(actual,wanted);
}
} // namespace portable_runtime
