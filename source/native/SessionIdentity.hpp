#pragma once
#include <cstdint>
#include <cstring>
#include "RuntimeLease.hpp"
namespace game_runtime {
struct session_identity {mesh_trial::request lease_identity={};uint32_t owner_pid=0;uint64_t owner_start_ticks=0;};
// Strict seven-field flat JSON. Unknown/duplicate fields, escape sequences and
// non-integer values fail closed. This is the pre-loader helper handoff only.
class session_parser {
 const char *p,*e;
 void ws(){while(p<e&&(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'))++p;}
 bool take(char c){ws();if(p==e||*p!=c)return false;++p;return true;}
 bool str(char *out,size_t cap){if(!take('"'))return false;size_t n=0;while(p<e&&*p!='"'){unsigned char c=*p++;if(c<32||c>126||c=='\\'||n+1>=cap)return false;out[n++]=char(c);}if(p==e)return false;++p;out[n]=0;return true;}
 bool number(uint64_t &n){ws();n=0;if(p==e||*p<'0'||*p>'9'||(*p=='0'&&p+1<e&&p[1]>='0'&&p[1]<='9'))return false;while(p<e&&*p>='0'&&*p<='9'){unsigned v=*p++-'0';if(n>(uint64_t(INT64_MAX)-v)/10)return false;n=n*10+v;}return true;}
 static bool hex(const char *s,size_t n){if(strlen(s)!=n)return false;for(size_t i=0;i<n;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
public:
 session_parser(const char*d,size_t n):p(d),e(d+n){}
 bool parse(session_identity &out){out={};if(e-p>4096||!take('{'))return false;uint32_t bits=0;for(unsigned i=0;i<7;++i){char key[40];uint32_t bit=0;uint64_t n=0;if(!str(key,sizeof(key))||!take(':'))return false;
  if(!strcmp(key,"version")){bit=1;if(!number(n)||n!=1)return false;out.lease_identity.version=1;}
  else if(!strcmp(key,"sessionId")){bit=2;if(!str(out.lease_identity.session,33)||!hex(out.lease_identity.session,32))return false;}
  else if(!strcmp(key,"contractSha256")){bit=4;if(!str(out.lease_identity.contract,65)||!hex(out.lease_identity.contract,64))return false;}
  else if(!strcmp(key,"targetPid")){bit=8;if(!number(n)||!n||n>UINT32_MAX)return false;out.lease_identity.target_pid=uint32_t(n);}
  else if(!strcmp(key,"targetStartUtc")){bit=16;if(!str(out.lease_identity.target_start_utc,40))return false;}
  else if(!strcmp(key,"ownerGuiPid")){bit=32;if(!number(n)||!n||n>UINT32_MAX)return false;out.owner_pid=uint32_t(n);}
  else if(!strcmp(key,"ownerGuiStartTicks")){bit=64;if(!number(n)||!n)return false;out.owner_start_ticks=n;}
  else return false;if(bits&bit)return false;bits|=bit;if(i!=6&&!take(','))return false;
 }if(bits!=127||!take('}'))return false;ws();return p==e;}
};
}
