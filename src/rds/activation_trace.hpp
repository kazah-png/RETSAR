#pragma once
#include <windows.h>

namespace seats::rds::activation {
// Method/phase strings supplied by this module are fixed literals. ETW is an
// observation channel only; it must not influence COM outputs or HRESULTs.
void write(const char* method,const char* phase,HRESULT result)noexcept;
void processAttach()noexcept;
void processDetach()noexcept;

template<size_t Length,class Function>
HRESULT invoke(const char (&method)[Length],Function&& function){
    write(method,"enter",S_OK);
    const HRESULT result=function();
    write(method,"return",result);
    return result;
}
}
