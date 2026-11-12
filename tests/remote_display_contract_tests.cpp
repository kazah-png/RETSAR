#include "../src/rds/remote_display_contract.hpp"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// INERT contract tests: anonymous event handles stand in for callback ownership.
// They are not input/display devices. No RDS, registration or driver API runs.
namespace {
using namespace seats::rds;
unsigned passed=0,failed=0,destroyed=0;
void check(bool value,const char* label){
    (value?passed:failed)++;
    if(!value)std::cerr<<"FAIL INERT remote display: "<<label<<'\n';
}
HANDLE newEvent(){return CreateEventW(nullptr,TRUE,FALSE,nullptr);}
bool valid(HANDLE handle){DWORD flags=0;return GetHandleInformation(handle,&flags)!=FALSE;}
HANDLE_PTR raw(HANDLE handle){return reinterpret_cast<HANDLE_PTR>(handle);}
class Owner final:public IUnknown {
    std::atomic<ULONG> references_{1};
public:
    RemoteDisplayContract display;
    explicit Owner(std::wstring_view id=L"RETSARRemoteIddLab"):display(*this,id){}
    ~Owner(){++destroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid==__uuidof(IUnknown))*out=static_cast<IUnknown*>(this);
        else if(iid==__uuidof(IWRdsWddmIddProps))*out=static_cast<IWRdsWddmIddProps*>(&display);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release()override{const ULONG remaining=--references_;if(!remaining)delete this;return remaining;}
};
}
int main(){
    auto* owner=new Owner;
    IWRdsWddmIddProps* props=nullptr;
    check(owner->QueryInterface(__uuidof(IWRdsWddmIddProps),reinterpret_cast<void**>(&props))==S_OK,"SDK interface QI");
    if(!props){owner->Release();return 1;}
    IUnknown* identity=nullptr;
    check(props->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&identity))==S_OK&&identity==static_cast<IUnknown*>(owner),"tear-off canonical owner identity");
    if(identity)identity->Release();
    check(props->QueryInterface(__uuidof(IUnknown),nullptr)==E_POINTER,"null QI output");
    wchar_t id[32]{};
    check(props->GetHardwareId(nullptr,32)==E_POINTER,"null HWID buffer");
    wchar_t canary=L'X';
    check(props->GetHardwareId(&canary,0)==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)&&canary==L'X',"zero buffer never written");
    wchar_t shortBuffer[2]={L'X',L'Y'};
    check(props->GetHardwareId(shortBuffer,2)==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)&&shortBuffer[0]==L'\0'&&shortBuffer[1]==L'Y',"short buffer is safely emptied");
    check(props->GetHardwareId(id,32)==S_OK&&std::wstring(id)==L"RETSARRemoteIddLab","bounded null-terminated remote HWID");
    for(auto invalid:{L"",L"RETSARIddLab",L"retsariddlab",L"not a hardware id"}){
        auto* rejected=new Owner(invalid);
        check(rejected->display.GetHardwareId(id,32)==E_INVALIDARG,"invalid or console HWID rejected");
        rejected->Release();
    }
    auto* oversized=new Owner(std::wstring(200,L'A'));
    check(oversized->display.GetHardwareId(id,32)==E_INVALIDARG,"oversized HWID rejected");
    oversized->Release();
    check(owner->display.bindSession(0)==E_INVALIDARG&&owner->display.bindSession(MAXDWORD)==E_INVALIDARG,"invalid remote session numbers rejected");
    HANDLE beforeReady=newEvent();
    check(props->OnDriverLoad(7,raw(beforeReady))==HRESULT_FROM_WIN32(ERROR_NOT_READY)&&!valid(beforeReady),"early callback consumes transferred handle without readiness");
    check(props->OnDriverLoad(7,0)==HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE),"null driver handle rejected");
    check(owner->display.bindSession(7)==S_OK&&props->EnableWddmIdd(TRUE)==S_OK,"bind actual-notification slot and enable mode");
    HANDLE wrongSession=newEvent();
    check(props->OnDriverLoad(8,raw(wrongSession))==E_INVALIDARG&&!valid(wrongSession),"wrong session transfer rejected and closed");
    HANDLE driver=newEvent();
    check(props->OnDriverLoad(7,raw(driver))==S_OK,"callback ownership retained");
    check(props->OnDriverLoad(7,raw(driver))==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)&&valid(driver),"duplicate exact handle never double-closed");
    HANDLE second=newEvent();
    check(props->OnDriverLoad(7,raw(second))==HRESULT_FROM_WIN32(ERROR_BUSY)&&!valid(second)&&valid(driver),"different concurrent load rejected without replacing owner");
    check(owner->display.bindSession(8)==HRESULT_FROM_WIN32(ERROR_BUSY),"live handle prevents session rebind");
    check(props->OnDriverUnload(8)==E_INVALIDARG&&valid(driver),"foreign-session unload cannot close handle");
    HANDLE copy=nullptr;
    check(owner->display.duplicateDriverHandle(7,&copy)==S_OK&&valid(copy),"worker gets real owned duplicate");
    HANDLE absent=reinterpret_cast<HANDLE>(1);
    check(owner->display.duplicateDriverHandle(8,&absent)==E_INVALIDARG&&!absent,"wrong-session copy clears output");
    check(owner->display.duplicateDriverHandle(7,nullptr)==E_POINTER,"null duplicate output");
    check(props->OnDriverUnload(7)==S_OK&&!valid(driver)&&valid(copy),"unload closes original while worker duplicate survives");
    check(SetEvent(copy)!=FALSE,"duplicate is a functioning kernel handle after unload");
    CloseHandle(copy);
    check(props->OnDriverUnload(7)==S_FALSE,"repeat unload is inert");
    auto state=owner->display.snapshot();
    check(state.enabled&&state.sessionBound&&state.sessionId==7&&!state.driverHandlePresent&&state.loadGeneration==1,"snapshot describes callbacks only");
    HANDLE reloaded=newEvent();
    check(props->OnDriverLoad(7,raw(reloaded))==S_OK&&props->EnableWddmIdd(FALSE)==S_OK&&!valid(reloaded),"mode disable releases transferred handle");
    props->EnableWddmIdd(TRUE);
    HANDLE raced=newEvent();
    check(props->OnDriverLoad(7,raw(raced))==S_OK,"handle prepared for concurrent lifecycle test");
    std::atomic<unsigned> unexpected{0};
    std::vector<std::thread> readers;
    for(unsigned i=0;i<4;++i)readers.emplace_back([&]{
        for(unsigned j=0;j<200;++j){
            HANDLE local=nullptr;const HRESULT result=owner->display.duplicateDriverHandle(7,&local);
            if(result==S_OK){if(!SetEvent(local))++unexpected;CloseHandle(local);}
            else if(result!=HRESULT_FROM_WIN32(ERROR_NOT_READY))++unexpected;
        }
    });
    owner->display.disconnect();
    for(auto& thread:readers)thread.join();
    check(unexpected==0&&!valid(raced)&&!owner->display.snapshot().sessionBound,"disconnect and worker duplication are serialized");
    HANDLE keyboard=newEvent(),mouse=newEvent();
    check(inspectInputHandlePrerequisites(0,raw(mouse),0)==InputHandlePrerequisite::MissingKeyboard,"missing keyboard rejected");
    check(inspectInputHandlePrerequisites(raw(keyboard),0,0)==InputHandlePrerequisite::MissingMouse,"missing mouse rejected");
    check(inspectInputHandlePrerequisites(raw(keyboard),raw(mouse),1)==InputHandlePrerequisite::BeepMustBeNull,"unused beep must be null");
    check(inspectInputHandlePrerequisites(raw(keyboard),raw(keyboard),0)==InputHandlePrerequisite::AliasedDevices,"aliased devices rejected");
    check(inspectInputHandlePrerequisites(raw(keyboard),raw(mouse),0)==InputHandlePrerequisite::DeviceAndSessionBindingUnverified,"valid event handles do not establish input devices or session ownership");
    CloseHandle(keyboard);
    check(inspectInputHandlePrerequisites(raw(keyboard),raw(mouse),0)==InputHandlePrerequisite::InvalidKeyboard,"closed input handle rejected");
    CloseHandle(mouse);
    props->Release();owner->Release();
    auto* destructorOwner=new Owner;
    destructorOwner->display.bindSession(9);destructorOwner->display.EnableWddmIdd(TRUE);
    HANDLE finalHandle=newEvent();destructorOwner->display.OnDriverLoad(9,raw(finalHandle));
    destructorOwner->Release();
    check(!valid(finalHandle)&&destroyed==7,"owner destruction releases remaining driver handle");
    std::cout<<"INERT remote display contract: "<<passed<<" passed, "<<failed<<" failed; no real driver, RDS session or input routing tested.\n";
    return failed?1:0;
}
