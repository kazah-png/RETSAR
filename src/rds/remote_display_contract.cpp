#include "remote_display_contract.hpp"
#include <algorithm>

namespace seats::rds {
namespace {
class ExclusiveLock {
    SRWLOCK& lock_;
public:
    explicit ExclusiveLock(SRWLOCK& lock) noexcept:lock_(lock){AcquireSRWLockExclusive(&lock_);}
    ~ExclusiveLock(){ReleaseSRWLockExclusive(&lock_);}
    ExclusiveLock(const ExclusiveLock&)=delete;
    ExclusiveLock& operator=(const ExclusiveLock&)=delete;
};
bool validHandle(HANDLE handle) noexcept {
    DWORD flags=0;
    return handle && handle!=INVALID_HANDLE_VALUE && GetHandleInformation(handle,&flags)!=FALSE;
}
}

RemoteDisplayContract::RemoteDisplayContract(IUnknown& owner,std::wstring_view id) noexcept:owner_(owner){
    if(id.empty() || id.size()>=hardwareId_.size())return;
    if(CompareStringOrdinal(id.data(),static_cast<int>(id.size()),L"RETSARIddLab",-1,TRUE)==CSTR_EQUAL)return;
    for(const auto ch:id){
        if(!((ch>=L'A'&&ch<=L'Z')||(ch>=L'a'&&ch<=L'z')||(ch>=L'0'&&ch<=L'9')||
            ch==L'_'||ch==L'-'||ch==L'\\'||ch==L'&'||ch==L'.'))return;
    }
    std::copy(id.begin(),id.end(),hardwareId_.begin());
    hardwareIdCharacters_=static_cast<DWORD>(id.size()+1);
}
RemoteDisplayContract::~RemoteDisplayContract(){disconnect();}
HRESULT STDMETHODCALLTYPE RemoteDisplayContract::QueryInterface(REFIID iid,void** out){
    if(!out)return E_POINTER;
    *out=nullptr;
    return owner_.QueryInterface(iid,out);
}
ULONG STDMETHODCALLTYPE RemoteDisplayContract::AddRef(){return owner_.AddRef();}
ULONG STDMETHODCALLTYPE RemoteDisplayContract::Release(){return owner_.Release();}
HRESULT STDMETHODCALLTYPE RemoteDisplayContract::GetHardwareId(WCHAR* out,DWORD count){
    if(!out)return E_POINTER;
    if(count)out[0]=L'\0';
    if(!hardwareIdCharacters_)return E_INVALIDARG;
    if(count<hardwareIdCharacters_)return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    std::copy_n(hardwareId_.begin(),hardwareIdCharacters_,out);
    return S_OK;
}
void RemoteDisplayContract::closeDriverLocked() noexcept {
    if(driver_){CloseHandle(driver_);driver_=nullptr;}
    state_.driverHandlePresent=false;
}
HRESULT RemoteDisplayContract::bindSession(ULONG sessionId) noexcept {
    if(sessionId==0 || sessionId==MAXDWORD)return E_INVALIDARG;
    ExclusiveLock lock(lock_);
    if(state_.sessionBound&&state_.sessionId==sessionId)return S_OK;
    if(driver_)return HRESULT_FROM_WIN32(ERROR_BUSY);
    state_.sessionId=sessionId;state_.sessionBound=true;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RemoteDisplayContract::EnableWddmIdd(BOOL enabled){
    ExclusiveLock lock(lock_);
    state_.enabled=enabled!=FALSE;
    if(!state_.enabled)closeDriverLocked();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RemoteDisplayContract::OnDriverLoad(ULONG sessionId,HANDLE_PTR value){
    HANDLE incoming=reinterpret_cast<HANDLE>(value);
    if(!validHandle(incoming))return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    ExclusiveLock lock(lock_);
    // This is already our handle on a duplicated notification; do not close it.
    if(incoming==driver_)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    HRESULT result=S_OK;
    if(!hardwareIdCharacters_ || !state_.enabled || !state_.sessionBound)result=HRESULT_FROM_WIN32(ERROR_NOT_READY);
    else if(sessionId!=state_.sessionId)result=E_INVALIDARG;
    else if(driver_)result=HRESULT_FROM_WIN32(ERROR_BUSY);
    if(FAILED(result)){
        // OnDriverLoad transfers ownership to the protocol stack, including a
        // callback rejected by this object. Never leak the transferred handle.
        CloseHandle(incoming);
        return result;
    }
    driver_=incoming;state_.driverHandlePresent=true;++state_.loadGeneration;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE RemoteDisplayContract::OnDriverUnload(ULONG sessionId){
    ExclusiveLock lock(lock_);
    if(!state_.sessionBound || sessionId!=state_.sessionId)return E_INVALIDARG;
    if(!driver_)return S_FALSE;
    closeDriverLocked();
    return S_OK;
}
void RemoteDisplayContract::disconnect() noexcept {
    ExclusiveLock lock(lock_);
    closeDriverLocked();
    state_.sessionBound=false;state_.sessionId=0;
}
RemoteDisplayState RemoteDisplayContract::snapshot() noexcept {
    ExclusiveLock lock(lock_);
    return state_;
}
HRESULT RemoteDisplayContract::duplicateDriverHandle(ULONG sessionId,HANDLE* out) noexcept {
    if(!out)return E_POINTER;
    *out=nullptr;
    ExclusiveLock lock(lock_);
    if(!state_.enabled || !state_.sessionBound || !driver_)return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if(sessionId!=state_.sessionId)return E_INVALIDARG;
    if(!DuplicateHandle(GetCurrentProcess(),driver_,GetCurrentProcess(),out,0,FALSE,DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}
InputHandlePrerequisite inspectInputHandlePrerequisites(HANDLE_PTR keyboard,HANDLE_PTR mouse,HANDLE_PTR beep) noexcept {
    const auto keyHandle=reinterpret_cast<HANDLE>(keyboard);
    const auto mouseHandle=reinterpret_cast<HANDLE>(mouse);
    if(!keyHandle||keyHandle==INVALID_HANDLE_VALUE)return InputHandlePrerequisite::MissingKeyboard;
    if(!mouseHandle||mouseHandle==INVALID_HANDLE_VALUE)return InputHandlePrerequisite::MissingMouse;
    if(beep)return InputHandlePrerequisite::BeepMustBeNull;
    if(keyHandle==mouseHandle)return InputHandlePrerequisite::AliasedDevices;
    if(!validHandle(keyHandle))return InputHandlePrerequisite::InvalidKeyboard;
    if(!validHandle(mouseHandle))return InputHandlePrerequisite::InvalidMouse;
    return InputHandlePrerequisite::DeviceAndSessionBindingUnverified;
}
} // namespace seats::rds
