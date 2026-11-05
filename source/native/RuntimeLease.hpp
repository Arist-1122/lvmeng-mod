#pragma once
#include <cstdint>
#include <cstring>
#include <limits>

namespace mesh_trial
{
struct request
{
    uint32_t version=0, target_pid=0;
    int64_t command_id=0, until_unix_ms=0;
    char session[33]={}, contract[65]={}, target_start_utc[40]={};
    bool replace=false;
};
// Strict bounded flat JSON for the eight fields written by the launcher. Escaped
// strings, duplicate/unknown fields, fractions and integer overflow are rejected.
class request_parser
{
    const char *p, *end;
    void whitespace() {while(p!=end&&(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'))++p;}
    bool take(char c) {whitespace();if(p==end||*p!=c)return false;++p;return true;}
    bool string(char *out,size_t capacity)
    {
        if(!take('"'))return false;size_t n=0;
        while(p!=end&&*p!='"') {unsigned char c=static_cast<unsigned char>(*p++);if(c<32||c>126||c=='\\'||n+1>=capacity)return false;out[n++]=char(c);}
        if(p==end)return false;++p;out[n]=0;return true;
    }
    bool integer(int64_t &value)
    {
        whitespace();if(p==end||*p<'0'||*p>'9')return false;value=0;
        if(*p=='0'&&p+1!=end&&p[1]>='0'&&p[1]<='9')return false;
        while(p!=end&&*p>='0'&&*p<='9') {const int digit=*p++-'0';if(value>(INT64_MAX-digit)/10)return false;value=value*10+digit;}
        return true;
    }
    static bool hex(const char *s,size_t n)
    {if(std::strlen(s)!=n)return false;for(size_t i=0;i<n;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;return true;}
public:
    request_parser(const char *data,size_t size):p(data),end(data+size){}
    bool parse(request &out)
    {
        out={};if(end-p>4096||!take('{'))return false;uint32_t fields=0;
        for(unsigned n=0;n<8;++n) {
            char key[32]={};if(!string(key,sizeof(key))||!take(':'))return false;uint32_t bit=0;int64_t number=0;
            if(!std::strcmp(key,"version")){bit=1;if(!integer(number)||number!=1)return false;out.version=1;}
            else if(!std::strcmp(key,"sessionId")){bit=2;if(!string(out.session,sizeof(out.session))||!hex(out.session,32))return false;}
            else if(!std::strcmp(key,"commandId")){bit=4;if(!integer(out.command_id)||out.command_id<=0)return false;}
            else if(!std::strcmp(key,"contractSha256")){bit=8;if(!string(out.contract,sizeof(out.contract))||!hex(out.contract,64))return false;}
            else if(!std::strcmp(key,"mode")){bit=16;char mode[16]={};if(!string(mode,sizeof(mode)))return false;if(!std::strcmp(mode,"replace"))out.replace=true;else if(std::strcmp(mode,"original"))return false;}
            else if(!std::strcmp(key,"enabledUntilUnixMs")){bit=32;if(!integer(out.until_unix_ms))return false;}
            else if(!std::strcmp(key,"targetPid")){bit=64;if(!integer(number)||number<=0||number>UINT32_MAX)return false;out.target_pid=uint32_t(number);}
            else if(!std::strcmp(key,"targetStartUtc")){bit=128;if(!string(out.target_start_utc,sizeof(out.target_start_utc)))return false;}
            else return false;
            if(fields&bit)return false;fields|=bit;
            if(n!=7&&!take(','))return false;
        }
        if(fields!=255||!take('}'))return false;whitespace();return p==end;
    }
};

class runtime_lease
{
    request _identity={},_last={};
    bool _enabled=false;
    uint64_t _tick_deadline=0;
    const char *_reason="original";
    static bool same(const char *a,const char *b) {return std::strcmp(a,b)==0;}
public:
    explicit runtime_lease(const request &identity):_identity(identity){}
    void reject(const char *reason) {_enabled=false;_reason=reason;}
    bool apply(const request &r,int64_t wall_ms,uint64_t tick_ms)
    {
        if(r.version!=1||r.target_pid!=_identity.target_pid||!same(r.session,_identity.session)||!same(r.contract,_identity.contract)||!same(r.target_start_utc,_identity.target_start_utc)) {reject("identity_or_contract");return false;}
        if(r.command_id<_last.command_id){reject("stale_command");return false;}
        if(r.command_id==_last.command_id) {
            if(r.replace!=_last.replace||r.until_unix_ms!=_last.until_unix_ms){reject("command_conflict");return false;}
            active(wall_ms,tick_ms);return true; // Never renew a repeated command ID.
        }
        _last=r;_enabled=false;_tick_deadline=0;
        if(!r.replace){_reason="original";return true;}
        if(wall_ms<0||r.until_unix_ms<=wall_ms||r.until_unix_ms-wall_ms>45000||tick_ms>UINT64_MAX-45000){_reason="invalid_lease";return false;}
        _tick_deadline=tick_ms+uint64_t(r.until_unix_ms-wall_ms);_enabled=true;_reason="replace_requested";return true;
    }
    bool active(int64_t wall_ms,uint64_t tick_ms)
    {
        if(_enabled&&(wall_ms>=_last.until_unix_ms||tick_ms>=_tick_deadline)){reject("lease_expired");}
        return _enabled;
    }
    int64_t acknowledged_command() const {return _last.command_id;}
    uint64_t monotonic_deadline() const {return _enabled?_tick_deadline:0;}
    const char *reason() const {return _reason;}
};
}
