#pragma once
#include <charconv>
#include <cstdint>

namespace beam_preview {
struct Window {
    uint64_t last_request = 0;
    uint64_t waiting_until = 0;
    uint64_t active_until = 0;

    uint64_t update(uint64_t now,uint64_t request,bool ready,bool focused,bool recent_body) {
        if (request>last_request && request<=now) {
            last_request=request;
            waiting_until=ready && now-request<45000 ? request+45000 : 0;
            active_until=0;
        }
        if (!ready) {
            waiting_until=active_until=0;
            return 0;
        }
        if (active_until) {
            if (!focused || now>=active_until) active_until=0;
        } else if (waiting_until) {
            if (now>=waiting_until) waiting_until=0;
            else if (focused && recent_body) {
                waiting_until=0;
                active_until=now+60000;
            }
        }
        return active_until;
    }
};

inline uint64_t parse(const char* text,unsigned length) {
    if (!text || length<3 || text[0]!='"' || text[length-1]!='"') return 0;
    uint64_t request=0;
    const auto last=text+length-1;
    const auto result=std::from_chars(text+1,last,request);
    return result.ec==std::errc{} && result.ptr==last ? request : 0;
}
}
