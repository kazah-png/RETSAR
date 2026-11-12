#pragma once
#include "child_session_status.hpp"
#include <cstddef>
#include <cwchar>

namespace seats::lab::child::client {
enum class Mode { NewChild, ResumeChild };
struct Request {Mode mode=Mode::NewChild;DWORD target=MAXDWORD;};
struct Row {DWORD id=MAXDWORD;int state=0;bool hasUser=false;};
inline const char* ModeName(Mode mode)noexcept {
    return mode==Mode::ResumeChild?"resume-existing-child":"new-child";
}
inline bool ParseTarget(const wchar_t* text,DWORD& output)noexcept {
    if(!text||!*text)return false;
    ULONGLONG value=0;
    for(const wchar_t* p=text;*p;++p) {
        if(*p<L'0'||*p>L'9')return false;
        value=value*10+static_cast<unsigned>(*p-L'0');
        if(value>0x7fffffff)return false;
    }
    if(!value)return false;output=static_cast<DWORD>(value);return true;
}
inline bool ParseModeArgs(int argc,const wchar_t* const* argv,Request& request,const wchar_t*& receipt)noexcept {
    request={};receipt=nullptr;
    if(!argv)return false;
    if(argc==4&&argv[1]&&argv[2]&&argv[3]&&argv[3][0]&&
       std::wcscmp(argv[1],L"--connect-child")==0&&std::wcscmp(argv[2],L"--receipt")==0) {
        receipt=argv[3];return true;
    }
    DWORD target=MAXDWORD;
    if(argc==5&&argv[1]&&argv[2]&&argv[3]&&argv[4]&&argv[4][0]&&
       std::wcscmp(argv[1],L"--resume-child-session")==0&&
       ParseTarget(argv[2],target)&&std::wcscmp(argv[3],L"--receipt")==0) {
        request={Mode::ResumeChild,target};receipt=argv[4];return true;
    }
    return false;
}
// Failure 1168 is permitted only for a NEW-session baseline as an observed
// unknown state. Resumption requires a successful exact parent-child relation.
inline bool AcceptSnapshot(const Request& request,const Snapshot& value,bool expectedEnabled=false)noexcept {
    if(!value.apiAvailable||!value.enabled.attempted||!value.enabled.succeeded||
       value.enabled.value!=expectedEnabled||!value.caller.attempted||!value.caller.succeeded||
       value.caller.value==0||value.caller.value==MAXDWORD||!value.console.attempted||
       value.caller.value!=value.console.value||!value.child.attempted)return false;
    if(request.mode==Mode::NewChild)
        return request.target==MAXDWORD&&((value.child.succeeded&&value.child.value==MAXDWORD)||
            (!value.child.succeeded&&value.child.error==ERROR_NOT_FOUND));
    if(request.mode!=Mode::ResumeChild||request.target==0||request.target==MAXDWORD||
       request.target==value.caller.value)return false;
    return value.child.succeeded&&value.child.value==request.target;
}
template<class RowT>
inline bool AcceptInventory(const Request& request,DWORD caller,bool complete,const RowT* rows,std::size_t count)noexcept {
    if(!complete||!rows||count==0||count>1024||caller==0||caller==MAXDWORD)return false;
    if(request.mode==Mode::NewChild&&request.target!=MAXDWORD)return false;
    if(request.mode==Mode::ResumeChild&&(request.target==0||request.target==MAXDWORD||request.target==caller))return false;
    if(request.mode!=Mode::NewChild&&request.mode!=Mode::ResumeChild)return false;
    bool callerSeen=false,targetSeen=false;
    for(std::size_t i=0;i<count;++i) {
        const auto& row=rows[i];
        for(std::size_t j=0;j<i;++j)if(rows[j].id==row.id)return false;
        if(row.id==caller) {
            if(row.state!=WTSActive||!row.hasUser)return false;
            callerSeen=true;
        }else if(request.mode==Mode::ResumeChild&&row.id==request.target) {
            if(row.state!=WTSDisconnected||!row.hasUser)return false;
            targetSeen=true;
        }else if(row.hasUser)return false;
    }
    return callerSeen&&(request.mode==Mode::NewChild||targetSeen);
}
template<class RowT>
inline bool AcceptObservedId(const Request& request,DWORD caller,DWORD id,const RowT* baseline,std::size_t count)noexcept {
    if(caller==0||caller==MAXDWORD||id==0||id==MAXDWORD||id==caller||!baseline||count==0||count>1024)return false;
    if(request.mode==Mode::ResumeChild) {
        if(id!=request.target)return false;
        for(std::size_t i=0;i<count;++i)
            if(baseline[i].id==id&&baseline[i].state==WTSDisconnected&&baseline[i].hasUser)return true;
        return false;
    }
    if(request.mode!=Mode::NewChild||request.target!=MAXDWORD)return false;
    for(std::size_t i=0;i<count;++i)if(baseline[i].id==id)return false;
    return true;
}
}

