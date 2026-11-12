#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <ostream>

namespace seats::lab::child::input::diagnostics {
inline bool Separated(POINT first,POINT second,LONG distance=16)noexcept {
    const LONGLONG dx=static_cast<LONGLONG>(first.x)-second.x;
    const LONGLONG dy=static_cast<LONGLONG>(first.y)-second.y;
    return dx>=distance||dx<=-static_cast<LONGLONG>(distance)||
        dy>=distance||dy<=-static_cast<LONGLONG>(distance);
}
// Coordinates here are all client coordinates. Selection never queries Windows
// or moves a cursor; the caller still checks the resulting screen hit window.
inline bool ChooseTarget(RECT client,POINT before,POINT original,POINT& target)noexcept {
    const LONGLONG width=static_cast<LONGLONG>(client.right)-client.left;
    const LONGLONG height=static_cast<LONGLONG>(client.bottom)-client.top;
    if(width<96||height<96||width>32768||height>32768)return false;
    const POINT candidates[]={
        {static_cast<LONG>(client.left+width/2),static_cast<LONG>(client.top+height/2)},
        {static_cast<LONG>(client.left+width*2/3),static_cast<LONG>(client.top+height/2)},
        {static_cast<LONG>(client.left+width/3),static_cast<LONG>(client.top+height/2)},
        {static_cast<LONG>(client.left+width/2),static_cast<LONG>(client.top+height*2/3)},
        {static_cast<LONG>(client.left+width/2),static_cast<LONG>(client.top+height/3)}};
    for(const auto point:candidates) {
        if(point.x<client.left+16||point.x>=client.right-16||
           point.y<client.top+16||point.y>=client.bottom-16)continue;
        if(Separated(point,before)&&Separated(point,original)){target=point;return true;}
    }
    return false;
}
struct Sample {ULONG_PTR extra=0;ULONGLONG elapsed=0;LONG x=0,y=0;bool afterSend=false;};
struct MouseMessages {
    static constexpr std::size_t Capacity=8;
    ULONGLONG total=0,afterSend=0,fullMatches=0,low32OnlyMatches=0,zeroAfterSend=0;
    std::array<Sample,Capacity> last{};
    std::size_t size=0;
    void Observe(ULONG_PTR extra,ULONG_PTR expected,bool sent,ULONGLONG elapsed,LONG x,LONG y)noexcept {
        ++total;
        if(sent) {
            ++afterSend;
            if(extra==expected)++fullMatches;
            else if(static_cast<DWORD>(extra)==static_cast<DWORD>(expected))++low32OnlyMatches;
            if(extra==0)++zeroAfterSend;
        }
        if(size==Capacity){for(std::size_t i=1;i<Capacity;++i)last[i-1]=last[i];--size;}
        last[size++]={extra,elapsed,x,y,sent};
    }
    void JsonMembers(std::ostream& out)const {
        out<<"\"mouseMoveAllCount\":"<<total<<",\"mouseMoveAfterSendCount\":"<<afterSend
           <<",\"mouseMarkerFullMatchCount\":"<<fullMatches
           <<",\"mouseMarkerLow32OnlyMatchCount\":"<<low32OnlyMatches
           <<",\"mouseMarkerZeroAfterSendCount\":"<<zeroAfterSend
           <<",\"mouseExtraInfoSamples\":[";
        for(std::size_t i=0;i<size;++i) {
            if(i)out<<',';
            const auto& sample=last[i];
            out<<"{\"extraInfo\":"<<sample.extra<<",\"elapsedMs\":"<<sample.elapsed
               <<",\"clientX\":"<<sample.x<<",\"clientY\":"<<sample.y
               <<",\"afterSend\":"<<(sample.afterSend?"true":"false")<<'}';
        }
        out<<']';
    }
};
}
