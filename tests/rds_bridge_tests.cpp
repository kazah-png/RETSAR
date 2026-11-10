#include "../src/rds/native_rds_bridge.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// All RDS callbacks in this executable are MOCK objects owned by this process.
// No registration, CoCreateInstance, RDS service or Windows session API is used.
namespace {
using namespace seats::rds;
unsigned passed=0,failed=0;
std::atomic<unsigned> mockObjects{0};

void check(bool condition,const char* description){
    if(condition)++passed;else ++failed;
    std::cout<<(condition?"PASS MOCK RDS ":"FAIL MOCK RDS ")<<description<<'\n';
}
void require(bool condition,const char* description){
    check(condition,description);
    if(!condition)throw std::runtime_error(description);
}

template<class Interface> class Ref {
    Interface* value_=nullptr;
public:
    Ref()=default;
    explicit Ref(Interface* value)noexcept:value_(value){}
    ~Ref(){reset();}
    Ref(const Ref&)=delete;
    Ref& operator=(const Ref&)=delete;
    Interface* get()const noexcept{return value_;}
    Interface* operator->()const noexcept{return value_;}
    explicit operator bool()const noexcept{return value_!=nullptr;}
    Interface** put()noexcept{reset();return &value_;}
    void reset(Interface* value=nullptr)noexcept{auto old=value_;value_=value;if(old)old->Release();}
};

using GetClassObject=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
using CanUnloadNow=HRESULT(STDAPICALLTYPE*)();
template<class Function> Function symbol(HMODULE module,const char* name){
    const auto address=GetProcAddress(module,name);Function result=nullptr;
    static_assert(sizeof(result)==sizeof(address));
    std::memcpy(&result,&address,sizeof(result));
    if(!result)throw std::runtime_error(std::string("Missing DLL export: ")+name);
    return result;
}

// Bounded barriers make an incorrect blocking cancellation fail instead of
// leaving the MOCK executable waiting indefinitely for its own callback.
class PausePoint {
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_=false,released_=false,timedOut_=false;
public:
    void pause(){
        std::unique_lock<std::mutex> lock(mutex_);entered_=true;changed_.notify_all();
        if(!changed_.wait_for(lock,std::chrono::seconds(5),[&]{return released_;}))timedOut_=true;
    }
    bool waitUntilEntered(){
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock,std::chrono::seconds(5),[&]{return entered_;});
    }
    void release(){std::lock_guard<std::mutex> lock(mutex_);released_=true;changed_.notify_all();}
    bool timedOut(){std::lock_guard<std::mutex> lock(mutex_);return timedOut_;}
};

class MockConnectionCallback final:public IWRdsProtocolConnectionCallback {
    std::atomic<ULONG> references_{1};
public:
    HRESULT idResult=S_OK,readyResult=S_OK,brokenResult=S_OK;
    ULONG id=41,idCalls=0,readyCalls=0,brokenCalls=0;
    bool brokenArgumentsValid=true;
    std::atomic<bool> redirectorNotificationActive{false};
    bool readyDuringNotification=false;
    PausePoint* idPause=nullptr;
    PausePoint* readyPause=nullptr;
    MockConnectionCallback(){++mockObjects;}
    ~MockConnectionCallback(){--mockObjects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IWRdsProtocolConnectionCallback))return E_NOINTERFACE;
        *out=static_cast<IWRdsProtocolConnectionCallback*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return references_.fetch_add(1,std::memory_order_relaxed)+1;}
    ULONG STDMETHODCALLTYPE Release()override{
        const auto left=references_.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!left)delete this;return left;
    }
    HRESULT STDMETHODCALLTYPE OnReady()override{
        ++readyCalls;readyDuringNotification=readyDuringNotification||redirectorNotificationActive.load();
        if(readyPause)readyPause->pause();return readyResult;
    }
    HRESULT STDMETHODCALLTYPE BrokenConnection(ULONG reason,ULONG source)override{
        ++brokenCalls;brokenArgumentsValid=brokenArgumentsValid&&reason==0&&source==0;return brokenResult;
    }
    HRESULT STDMETHODCALLTYPE StopScreenUpdates()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RedrawWindow(WRDS_SMALL_RECT*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetConnectionId(ULONG* out)override{
        ++idCalls;if(!out)return E_POINTER;*out=0;
        if(idPause)idPause->pause();if(SUCCEEDED(idResult))*out=id;return idResult;
    }
};

class MockListenerCallback final:public IWRdsProtocolListenerCallback {
    std::atomic<ULONG> references_{1};
public:
    Ref<MockConnectionCallback> callback{new MockConnectionCallback};
    Ref<IWRdsProtocolConnection> connection;
    HRESULT connectedResult=S_OK;
    ULONG connectedCalls=0;
    bool settingsValid=false;
    bool notifyRedirectorSynchronously=true,redirectorOutputCleared=true;
    ULONG redirectorCalls=0;
    HRESULT redirectorResult=E_PENDING;
    PausePoint* connectedPause=nullptr;
    MockListenerCallback(){++mockObjects;}
    ~MockListenerCallback(){--mockObjects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IWRdsProtocolListenerCallback))return E_NOINTERFACE;
        *out=static_cast<IWRdsProtocolListenerCallback*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return references_.fetch_add(1,std::memory_order_relaxed)+1;}
    ULONG STDMETHODCALLTYPE Release()override{
        const auto left=references_.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!left)delete this;return left;
    }
    HRESULT notifyRedirector(){
        if(!connection)return E_UNEXPECTED;
        const auto sentinel=reinterpret_cast<IWRdsProtocolLogonErrorRedirector*>(static_cast<ULONG_PTR>(1));
        auto redirector=sentinel;++redirectorCalls;
        callback->redirectorNotificationActive.store(true);
        redirectorResult=connection->GetLogonErrorRedirector(&redirector);
        callback->redirectorNotificationActive.store(false);
        redirectorOutputCleared=redirectorOutputCleared&&!redirector;
        if(redirector&&redirector!=sentinel)redirector->Release();
        return redirectorResult;
    }
    HRESULT STDMETHODCALLTYPE OnConnected(IWRdsProtocolConnection* candidate,PWRDS_CONNECTION_SETTINGS settings,
        IWRdsProtocolConnectionCallback** out)override{
        ++connectedCalls;if(!out)return E_POINTER;*out=nullptr;
        settingsValid=settings&&settings->WRdsConnectionSettingLevel==WRDS_CONNECTION_SETTING_LEVEL_1&&
            settings->WRdsConnectionSetting.WRdsConnectionSettings1.WRdsListenerSettings.WRdsListenerSettingLevel==WRDS_LISTENER_SETTING_LEVEL_1;
        if(FAILED(connectedResult))return connectedResult;
        if(!candidate||!settingsValid)return E_INVALIDARG;
        if(connectedPause)connectedPause->pause();
        candidate->AddRef();connection.reset(candidate);
        *out=callback.get();(*out)->AddRef();
        if(notifyRedirectorSynchronously)notifyRedirector();
        return S_OK;
    }
};

struct Fixture {
    Ref<IWRdsProtocolManager> manager;
    Ref<IWRdsProtocolListener> listener;
    Ref<IRetsarRdsLabControl> control;
    Ref<MockListenerCallback> host{new MockListenerCallback};
    explicit Fixture(GetClassObject getFactory,bool listen=true){
        Ref<IClassFactory> factory;
        if(FAILED(getFactory(NativeRdsBridgeClass,__uuidof(IClassFactory),reinterpret_cast<void**>(factory.put()))))
            throw std::runtime_error("MOCK fixture factory creation failed");
        if(FAILED(factory->CreateInstance(nullptr,__uuidof(IWRdsProtocolManager),reinterpret_cast<void**>(manager.put()))))
            throw std::runtime_error("MOCK fixture manager creation failed");
        wchar_t name[]=L"RETSAR-Lab";
        if(FAILED(manager->CreateListener(name,listener.put())))throw std::runtime_error("MOCK fixture listener creation failed");
        if(FAILED(listener->QueryInterface(__uuidof(IRetsarRdsLabControl),reinterpret_cast<void**>(control.put()))))
            throw std::runtime_error("MOCK fixture laboratory interface missing");
        if(listen&&FAILED(listener->StartListen(host.get())))throw std::runtime_error("MOCK fixture StartListen failed");
    }
    ~Fixture(){
        if(host&&host->connection)host->connection->Close();
        if(listener)listener->StopListen();
        host.reset();control.reset();listener.reset();manager.reset();
    }
    ExperimentOutcome start(ULONGLONG operation=1){
        ExperimentOutcome outcome{};
        if(FAILED(control->StartExperiment(TRUE,operation,&outcome)))throw std::runtime_error("MOCK fixture experiment start failed");
        return outcome;
    }
    std::vector<BridgeEvent> drain(ULONGLONG* droppedOut=nullptr){
        std::array<BridgeEvent,EventCapacity> events{};ULONG written=0;ULONGLONG dropped=0;
        if(FAILED(control->DrainEvents(events.data(),EventCapacity,&written,&dropped)))throw std::runtime_error("MOCK fixture event drain failed");
        if(written>EventCapacity)throw std::runtime_error("MOCK fixture drain exceeded capacity");
        if(droppedOut)*droppedOut=dropped;
        return {events.begin(),events.begin()+written};
    }
};

void factoryTests(GetClassObject getFactory,CanUnloadNow canUnload){
    check(canUnload()==S_OK,"fresh DLL has no live objects");
    IClassFactory* invalid=reinterpret_cast<IClassFactory*>(static_cast<ULONG_PTR>(1));
    const GUID unknown{};
    check(getFactory(unknown,__uuidof(IClassFactory),reinterpret_cast<void**>(&invalid))==CLASS_E_CLASSNOTAVAILABLE&&!invalid,
        "unknown class clears output and is rejected");
    check(getFactory(NativeRdsBridgeClass,__uuidof(IClassFactory),nullptr)==E_POINTER,"factory null output rejected");
    Ref<IClassFactory> factory;
    require(getFactory(NativeRdsBridgeClass,__uuidof(IClassFactory),reinterpret_cast<void**>(factory.put()))==S_OK&&factory,
        "DLL factory export returns the SDK IClassFactory interface");
    check(canUnload()==S_FALSE,"live factory prevents unload");
    Ref<IUnknown> first,second;
    require(factory->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(first.put()))==S_OK,"factory exposes IUnknown");
    require(first->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(second.put()))==S_OK,"factory IUnknown can be queried again");
    check(first.get()==second.get(),"factory IUnknown identity is canonical");
    first.reset();second.reset();
    check(factory->LockServer(TRUE)==S_OK,"server lock can be acquired");
    factory.reset();check(canUnload()==S_FALSE,"server lock keeps DLL loaded after factory release");
    require(getFactory(NativeRdsBridgeClass,__uuidof(IClassFactory),reinterpret_cast<void**>(factory.put()))==S_OK,"factory can be reacquired");
    check(factory->LockServer(FALSE)==S_OK,"server lock can be released");
    Ref<MockConnectionCallback> outer{new MockConnectionCallback};
    IWRdsProtocolManager* aggregate=reinterpret_cast<IWRdsProtocolManager*>(static_cast<ULONG_PTR>(1));
    check(factory->CreateInstance(outer.get(),__uuidof(IWRdsProtocolManager),reinterpret_cast<void**>(&aggregate))==CLASS_E_NOAGGREGATION&&!aggregate,
        "aggregation refused with initialized output");
    Ref<IWRdsProtocolManager> manager;
    require(factory->CreateInstance(nullptr,__uuidof(IWRdsProtocolManager),reinterpret_cast<void**>(manager.put()))==S_OK&&manager,
        "factory constructs the SDK manager object");
    void* unsupported=reinterpret_cast<void*>(static_cast<ULONG_PTR>(1));
    check(manager->QueryInterface(unknown,&unsupported)==E_NOINTERFACE&&!unsupported,"unsupported manager QI clears output");
    factory.reset();check(canUnload()==S_FALSE,"manager reference prevents unload");
    manager.reset();outer.reset();check(canUnload()==S_OK,"released manager and factory allow unload");
}

void gateAndSuccessTests(GetClassObject getFactory){
    Fixture fixture(getFactory,false);ExperimentOutcome outcome{};
    check(fixture.control->StartExperiment(TRUE,1,&outcome)==HRESULT_FROM_WIN32(ERROR_NOT_READY),"attempt before StartListen is not ready");
    require(fixture.listener->StartListen(fixture.host.get())==S_OK,"StartListen accepts the MOCK callback");
    check(fixture.host->connectedCalls==0,"StartListen never starts a connection automatically");
    Ref<IUnknown> viaListener,viaControl;
    require(fixture.listener->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(viaListener.put()))==S_OK,"listener exposes IUnknown");
    require(fixture.control->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(viaControl.put()))==S_OK,"laboratory interface exposes IUnknown");
    check(viaListener.get()==viaControl.get(),"listener and laboratory interfaces share canonical IUnknown");
    check(fixture.control->StartExperiment(FALSE,1,&outcome)==E_ACCESSDENIED&&fixture.host->connectedCalls==0,
        "explicit experiment gate refuses without consuming the attempt");
    check(fixture.control->StartExperiment(TRUE,0,&outcome)==E_INVALIDARG&&fixture.host->connectedCalls==0,"zero operation ID refused");
    require(fixture.control->StartExperiment(TRUE,71,&outcome)==S_OK,"explicit MOCK connection sequence succeeds");
    check(fixture.host->settingsValid,"connection and listener setting levels are initialized");
    check(fixture.host->connectedCalls==1&&fixture.host->callback->idCalls==1&&fixture.host->callback->readyCalls==1,
        "OnConnected GetConnectionId and OnReady each called exactly once");
    check(fixture.host->redirectorCalls==1&&fixture.host->redirectorResult==E_NOTIMPL&&fixture.host->redirectorOutputCleared,
        "synchronous logon-error notification allows the documented null redirector");
    check(outcome.logonErrorRedirectorObserved&&!fixture.host->callback->readyDuringNotification,
        "OnReady waits until the logon-error notification has returned");
    check(outcome.operationId==71&&outcome.connectionId==41&&outcome.onConnectedAccepted&&outcome.readyAccepted,
        "outcome reports observed MOCK callback acceptance");
    check(!outcome.sessionOwnershipProven,"MOCK callback success never asserts session ownership");
    check(fixture.control->StartExperiment(TRUE,71,&outcome)==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)&&fixture.host->connectedCalls==1,
        "same operation cannot initiate a second connection");
    check(fixture.control->StartExperiment(TRUE,72,&outcome)==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)&&fixture.host->connectedCalls==1,
        "listener cannot disguise a second attempt with a new operation ID");
    const auto events=fixture.drain();
    const auto where=[&](EventKind kind){return std::find_if(events.begin(),events.end(),[&](const BridgeEvent& event){return event.kind==kind;});};
    const auto connected=where(EventKind::OnConnectedReturned),identified=where(EventKind::ConnectionIdObserved),ready=where(EventKind::OnReadyReturned);
    check(connected!=events.end()&&identified!=events.end()&&ready!=events.end()&&connected<identified&&identified<ready,
        "queued MOCK handshake observations retain their order");
    const auto redirector=where(EventKind::LogonErrorRedirectorObserved);
    check(redirector!=events.end()&&ready!=events.end()&&redirector<ready,
        "logon-error notification is observed before readiness");
}

void delayedHandshakeTests(GetClassObject getFactory){
    Fixture fixture(getFactory);fixture.host->notifyRedirectorSynchronously=false;ExperimentOutcome outcome{};
    check(fixture.control->StartExperiment(TRUE,75,&outcome)==E_PENDING&&outcome.onConnectedAccepted&&!outcome.readyAccepted,
        "missing logon-error notification leaves an accepted connection pending");
    check(fixture.host->callback->readyCalls==0&&!outcome.logonErrorRedirectorObserved,
        "pending handshake does not call OnReady prematurely");
    check(fixture.control->ContinueExperiment(&outcome)==E_PENDING&&fixture.host->callback->readyCalls==0,
        "explicit continuation remains pending until the required notification");
    check(fixture.host->notifyRedirector()==E_NOTIMPL&&fixture.host->redirectorOutputCleared,
        "delayed logon-error notification returns a null optional redirector");
    check(fixture.host->callback->readyCalls==0&&!fixture.host->callback->readyDuringNotification,
        "notification itself never reenters OnReady");
    check(fixture.control->ContinueExperiment(&outcome)==S_OK&&outcome.readyAccepted&&outcome.logonErrorRedirectorObserved,
        "explicit continuation completes the notified MOCK handshake");
    check(outcome.operationId==75&&!outcome.sessionOwnershipProven&&fixture.host->callback->idCalls==1&&fixture.host->callback->readyCalls==1,
        "delayed continuation preserves operation and exactly-once callbacks without ownership claims");
    fixture.control->ContinueExperiment(&outcome);
    check(fixture.host->callback->readyCalls==1,"a second continuation cannot repeat OnReady");
}

void failureTests(GetClassObject getFactory){
    {
        Fixture fixture(getFactory);fixture.host->connectedResult=E_ACCESSDENIED;ExperimentOutcome outcome{};
        const auto result=fixture.control->StartExperiment(TRUE,1,&outcome);
        check(result==E_ACCESSDENIED&&outcome.onConnectedResult==E_ACCESSDENIED&&!outcome.onConnectedAccepted&&!outcome.readyAccepted,
            "OnConnected failure preserved instead of invented success");
        check(fixture.host->callback->idCalls==0&&fixture.host->callback->readyCalls==0,"failed OnConnected stops later callbacks");
        check(fixture.control->StartExperiment(TRUE,2,&outcome)==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),"failed creation attempt is not blindly retried");
    }
    {
        Fixture fixture(getFactory);fixture.host->callback->idResult=E_FAIL;ExperimentOutcome outcome{};
        const auto result=fixture.control->StartExperiment(TRUE,3,&outcome);
        check(result==E_FAIL&&outcome.onConnectedAccepted&&outcome.connectionIdResult==E_FAIL&&!outcome.readyAccepted,
            "GetConnectionId failure remains observable");
        check(fixture.host->callback->readyCalls==0,"failed identity lookup does not call OnReady");
    }
    {
        Fixture fixture(getFactory);fixture.host->callback->readyResult=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);ExperimentOutcome outcome{};
        const auto result=fixture.control->StartExperiment(TRUE,4,&outcome);
        check(result==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)&&outcome.onReadyResult==result&&!outcome.readyAccepted,
            "OnReady refusal is not promoted to connected or owned");
        check(fixture.host->connectedCalls==1&&fixture.host->callback->idCalls==1&&fixture.host->callback->readyCalls==1,
            "failed readiness keeps the one-attempt sequence");
    }
}

void unavailableTests(GetClassObject getFactory){
    Fixture fixture(getFactory);fixture.start();auto connection=fixture.host->connection.get();
    WRDS_USER_CREDENTIAL credentials;std::memset(&credentials,0x5a,sizeof(credentials));
    WRDS_USER_CREDENTIAL zero{};
    check(connection->GetUserCredentials(&credentials)==E_NOTIMPL&&std::memcmp(&credentials,&zero,sizeof(zero))==0,
        "unimplemented credentials clear the output and fail explicitly");
    IWRdsProtocolLicenseConnection* license=reinterpret_cast<IWRdsProtocolLicenseConnection*>(static_cast<ULONG_PTR>(1));
    check(connection->GetLicenseConnection(&license)==E_NOTIMPL&&!license,"missing license transport never returns placeholder success");
    HANDLE_PTR keyboard=1,mouse=2,beep=3;
    check(connection->GetInputHandles(&keyboard,&mouse,&beep)==E_NOTIMPL&&!keyboard&&!mouse&&!beep,
        "missing input transport clears every handle");
    IWRdsWddmIddProps* props=reinterpret_cast<IWRdsWddmIddProps*>(static_cast<ULONG_PTR>(1));
    check(connection->QueryInterface(__uuidof(IWRdsWddmIddProps),reinterpret_cast<void**>(&props))==E_NOINTERFACE&&!props,
        "unimplemented IDD interface is not advertised");
}

WRDS_SESSION_ID session(ULONG id,ULONG discriminator){
    WRDS_SESSION_ID value{};value.SessionId=id;
    value.SessionUniqueGuid={discriminator,0x1234,0x4567,{0x89,0xab,0xcd,0xef,0x10,0x20,0x30,0x40}};
    return value;
}
void observationTests(GetClassObject getFactory){
    Fixture fixture(getFactory);fixture.start(81);fixture.drain();
    auto first=session(9,101),rebound=session(9,202);auto connection=fixture.host->connection.get();
    const auto outboundBefore=fixture.host->callback->readyCalls+fixture.host->callback->idCalls+fixture.host->callback->brokenCalls;
    check(connection->NotifySessionId(&first,0)==S_OK&&connection->NotifySessionId(&rebound,0)==S_OK,
        "MOCK notifications accept session identity and a changed GUID");
    WRDS_CONNECTION_SETTINGS settings{};wchar_t empty[]=L"";
    check(connection->LogonNotify(0,empty,empty,&rebound,&settings)==S_OK,"MOCK logon notification is recorded without account data");
    const auto events=fixture.drain();
    const auto assigned=std::count_if(events.begin(),events.end(),[](const BridgeEvent& value){return value.kind==EventKind::SessionAssigned;});
    check(assigned==2,"rebind remains two observations rather than one permanent numeric identity");
    const auto oldIdentity=std::find_if(events.begin(),events.end(),[&](const BridgeEvent& event){return event.hasSession&&event.session.SessionUniqueGuid==first.SessionUniqueGuid;});
    const auto newIdentity=std::find_if(events.begin(),events.end(),[&](const BridgeEvent& event){return event.hasSession&&event.session.SessionUniqueGuid==rebound.SessionUniqueGuid;});
    check(oldIdentity!=events.end()&&newIdentity!=events.end()&&oldIdentity->session.SessionId==9&&newIdentity->session.SessionId==9,
        "both full GUID plus ID pairs survive the bounded queue");
    check(std::all_of(events.begin(),events.end(),[](const BridgeEvent& event){return event.operationId==81;}),"events correlate to the explicit operation");
    const auto outboundAfter=fixture.host->callback->readyCalls+fixture.host->callback->idCalls+fixture.host->callback->brokenCalls;
    check(outboundBefore==outboundAfter,"notification handlers do not reenter outbound RDS callbacks");
    check(connection->NotifySessionId(nullptr,0)==E_POINTER,"null notification identity rejected");
}

void queueTests(GetClassObject getFactory){
    Fixture fixture(getFactory);fixture.start(91);fixture.drain();auto connection=fixture.host->connection.get();
    bool accepted=true;
    for(ULONG i=0;i<EventCapacity;++i){auto identity=session(i+1,i+1);accepted=SUCCEEDED(connection->NotifySessionId(&identity,0))&&accepted;}
    check(accepted,"bounded queue accepts exactly its available capacity");
    auto extra=session(1000,1000);
    check(connection->NotifySessionId(&extra,0)==HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),"full event queue reports overflow explicitly");
    ULONGLONG dropped=0;const auto events=fixture.drain(&dropped);
    check(events.size()==EventCapacity&&dropped>=1,"drain reports retained events and lost observations");
    bool increasing=true;
    for(size_t i=1;i<events.size();++i)increasing=increasing&&events[i-1].sequence<events[i].sequence;
    check(increasing,"retained event sequence numbers are strictly increasing");
    check(connection->NotifySessionId(&extra,0)==S_OK,"draining restores queue capacity without reissuing a connection");
    check(fixture.host->connectedCalls==1,"queue overflow cannot cause a new connection attempt");
}

void cancellationTests(GetClassObject getFactory){
    Fixture fixture(getFactory);fixture.start();const auto before=fixture.host->callback->brokenCalls;
    check(SUCCEEDED(fixture.control->CancelExperiment()),"explicit cancellation is accepted for the owned MOCK connection");
    check(fixture.host->callback->brokenCalls==before+1&&fixture.host->callback->brokenArgumentsValid,
        "cancellation notifies only the retained connection callback");
    const auto once=fixture.host->callback->brokenCalls;
    check(SUCCEEDED(fixture.control->CancelExperiment())&&fixture.host->callback->brokenCalls==once,"repeated cancellation does not duplicate BrokenConnection");
    ExperimentOutcome outcome{};
    check(FAILED(fixture.control->StartExperiment(TRUE,999,&outcome))&&fixture.host->connectedCalls==1,"cancellation cannot silently restart a listener attempt");
    check(SUCCEEDED(fixture.host->connection->Close()),"Close records closure of the MOCK connection");
    check(SUCCEEDED(fixture.listener->StopListen()),"StopListen releases the listener callback without a session API");
}

void failedCancellationTests(GetClassObject getFactory){
    Fixture fixture(getFactory);fixture.start();fixture.host->callback->brokenResult=E_FAIL;
    check(fixture.control->CancelExperiment()==E_FAIL,"BrokenConnection failure is returned to the cancelling caller");
    check(fixture.control->CancelExperiment()==E_FAIL&&fixture.host->callback->brokenCalls==1,
        "repeated cancellation preserves the failure without repeating BrokenConnection");
}

enum class PausedCallback { Connected,Identity,Ready,ContinuedReady };
void cancellationWhileCallbackTests(GetClassObject getFactory){
    constexpr PausedCallback stages[]={PausedCallback::Connected,PausedCallback::Identity,PausedCallback::Ready,PausedCallback::ContinuedReady};
    for(const auto stage:stages){
        PausePoint pause;Fixture fixture(getFactory);
        if(stage==PausedCallback::Connected)fixture.host->connectedPause=&pause;
        else if(stage==PausedCallback::Identity)fixture.host->callback->idPause=&pause;
        else fixture.host->callback->readyPause=&pause;
        if(stage==PausedCallback::ContinuedReady){
            fixture.host->notifyRedirectorSynchronously=false;ExperimentOutcome pending{};
            require(fixture.control->StartExperiment(TRUE,125,&pending)==E_PENDING,"continuation-race setup is pending");
            require(fixture.host->notifyRedirector()==E_NOTIMPL,"continuation-race notification is observed");
        }
        HRESULT workerInitialized=E_UNEXPECTED,workerResult=E_UNEXPECTED;ExperimentOutcome outcome{};
        std::thread worker([&]{
            workerInitialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            if(SUCCEEDED(workerInitialized)){
                workerResult=stage==PausedCallback::ContinuedReady?fixture.control->ContinueExperiment(&outcome):
                    fixture.control->StartExperiment(TRUE,120,&outcome);
                CoUninitialize();
            }
        });
        const bool entered=pause.waitUntilEntered();
        const auto cancellation=entered?fixture.control->CancelExperiment():E_UNEXPECTED;
        pause.release();worker.join();
        check(entered&&SUCCEEDED(workerInitialized)&&!pause.timedOut(),"MTA cancellation meets a bounded paused MOCK callback");
        check(cancellation==E_PENDING,"in-flight cancellation returns pending without waiting on the callback");
        check(workerResult==HRESULT_FROM_WIN32(ERROR_CANCELLED)&&outcome.cancellationRequested,
            "active handshake cannot return success after concurrent cancellation");
        const ULONG expectedReady=stage==PausedCallback::Ready||stage==PausedCallback::ContinuedReady?1:0;
        check(fixture.host->callback->readyCalls==expectedReady,
            "cancellation before OnReady prevents it; cancellation inside it does not call it again");
        check(fixture.host->callback->brokenCalls==1&&fixture.host->callback->brokenArgumentsValid,
            "active handshake completes one cancellation on its retained MOCK connection");
        check(fixture.control->CancelExperiment()==S_OK&&fixture.host->callback->brokenCalls==1,
            "completed concurrent cancellation remains idempotent");
    }
}

void stopWhileConnectedTests(GetClassObject getFactory){
    PausePoint pause;Fixture fixture(getFactory);fixture.host->connectedPause=&pause;
    HRESULT workerInitialized=E_UNEXPECTED,workerResult=E_UNEXPECTED;ExperimentOutcome outcome{};
    std::thread worker([&]{
        workerInitialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(SUCCEEDED(workerInitialized)){
            workerResult=fixture.control->StartExperiment(TRUE,130,&outcome);
            CoUninitialize();
        }
    });
    const bool entered=pause.waitUntilEntered();
    const auto stopped=entered?fixture.listener->StopListen():E_UNEXPECTED;
    pause.release();worker.join();
    check(entered&&SUCCEEDED(workerInitialized)&&!pause.timedOut(),"StopListen meets an in-flight OnConnected without a blocking wait");
    check(stopped==S_OK||stopped==E_PENDING,"StopListen accepts stopping the in-process listener");
    check(workerResult==HRESULT_FROM_WIN32(ERROR_CANCELLED)&&fixture.host->callback->readyCalls==0,
        "OnConnected returning after StopListen cannot resurrect the readiness sequence");
    check(fixture.host->callback->brokenCalls==1&&fixture.host->callback->brokenArgumentsValid,
        "late OnConnected result is cancelled through its local callback once");
    fixture.control->ContinueExperiment(&outcome);
    check(fixture.host->callback->readyCalls==0&&fixture.host->callback->brokenCalls==1,
        "continuing a stopped listener cannot restore or duplicate its callbacks");
}
}

int wmain(int argc,wchar_t** argv){
    HMODULE module=nullptr;CanUnloadNow canUnload=nullptr;bool comInitialized=false;
    try{
        if(argc!=2)throw std::invalid_argument("MOCK harness requires an explicit C: path to the lab DLL.");
        const std::wstring path=argv[1];
        if(path.size()<4||(path[0]!=L'C'&&path[0]!=L'c')||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))
            throw std::invalid_argument("MOCK harness only loads the explicitly built DLL on C:.");
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(initialized))throw std::runtime_error("Cannot initialize the MOCK harness MTA.");
        comInitialized=true;
        module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("Cannot load the explicitly supplied MOCK-test DLL.");
        const auto getFactory=symbol<GetClassObject>(module,"DllGetClassObject");
        canUnload=symbol<CanUnloadNow>(module,"DllCanUnloadNow");
        factoryTests(getFactory,canUnload);
        gateAndSuccessTests(getFactory);
        delayedHandshakeTests(getFactory);
        failureTests(getFactory);
        unavailableTests(getFactory);
        observationTests(getFactory);
        queueTests(getFactory);
        cancellationTests(getFactory);
        failedCancellationTests(getFactory);
        cancellationWhileCallbackTests(getFactory);
        stopWhileConnectedTests(getFactory);
        check(mockObjects.load()==0,"all MOCK callback references released");
        check(canUnload()==S_OK,"all COM bridge objects and server locks released");
    }catch(const std::exception& error){++failed;std::cerr<<"FAIL MOCK RDS fatal: "<<error.what()<<'\n';}
    // Do not unload code if a reference-lifetime regression left an object alive.
    if(module&&canUnload&&canUnload()==S_OK)FreeLibrary(module);
    if(comInitialized)CoUninitialize();
    std::cout<<"MOCK RDS TOTAL "<<passed<<" passed, "<<failed
        <<" failed. In-process fake callbacks only; no registration, RDS service, Windows session or device mutation.\n";
    return failed?1:0;
}
