#include "native_rds_bridge.hpp"
#include "activation_trace.hpp"
#include "lab_control_worker.hpp"
#include <TraceLoggingProvider.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <string>


namespace seats::rds::activation {
TRACELOGGING_DEFINE_PROVIDER(
    provider,
    "RETSAR.RdsActivation",
    (0x77e2b2c5,0x62f8,0x4c0c,0xab,0x1f,0x94,0xc6,0xd4,0x91,0x73,0xe1));

void write(const char* method,const char* phase,HRESULT result)noexcept{
    const auto previousError=GetLastError();
    TraceLoggingWrite(provider,"Activation",
        TraceLoggingLevel(4),
        TraceLoggingKeyword(0x1),
        TraceLoggingString(method,"Method"),
        TraceLoggingString(phase,"Phase"),
        TraceLoggingUInt32(static_cast<ULONG>(result),"HResult"));
    SetLastError(previousError);
}
void connectionInterface(REFIID iid)noexcept{
    const auto previousError=GetLastError();
    TraceLoggingWrite(provider,"ConnectionInterface",
        TraceLoggingLevel(4),TraceLoggingKeyword(0x1),
        TraceLoggingGuid(iid,"InterfaceId"));
    SetLastError(previousError);
}
void processAttach()noexcept{
    const auto previousError=GetLastError();
    // Diagnostic failure must not fail DLL loading or change provider behavior.
    (void)TraceLoggingRegister(provider);
    write("DLL.ProcessAttach","event",S_OK);
    SetLastError(previousError);
}
void processDetach()noexcept{
    const auto previousError=GetLastError();
    write("DLL.ProcessDetach","event",S_OK);
    // Required before unloading, including when registration failed.
    TraceLoggingUnregister(provider);
    SetLastError(previousError);
}
}

// Original RETSAR implementation against Windows SDK interfaces. No code from
// the Azure protocol sample is incorporated in this translation unit.
namespace seats::rds {
namespace {
std::atomic<ULONG> liveObjects{0},serverLocks{0};
thread_local unsigned notificationDepth=0;
struct NotificationScope {NotificationScope(){++notificationDepth;}~NotificationScope(){--notificationDepth;}};
thread_local unsigned controlDepth=0;
struct ControlScope {ControlScope(){++controlDepth;}~ControlScope(){--controlDepth;}};
constexpr HRESULT queueFull=HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

template<class Function> HRESULT boundary(Function&& function)noexcept{
    try{return function();}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
HRESULT requireMta()noexcept{
    APTTYPE type=APTTYPE_CURRENT;APTTYPEQUALIFIER qualifier=APTTYPEQUALIFIER_NONE;
    const auto result=CoGetApartmentType(&type,&qualifier);
    if(FAILED(result))return result;
    return type==APTTYPE_MTA?S_OK:RPC_E_WRONG_THREAD;
}
template<class Interface> struct InterfaceRef {
    Interface* pointer;
    explicit InterfaceRef(Interface* value):pointer(value){pointer->AddRef();}
    ~InterfaceRef(){pointer->Release();}
    InterfaceRef(const InterfaceRef&)=delete;
    InterfaceRef& operator=(const InterfaceRef&)=delete;
};
template<class Interface> using SharedInterface=std::shared_ptr<InterfaceRef<Interface>>;
template<class Interface> SharedInterface<Interface> retain(Interface* pointer){
    return std::make_shared<InterfaceRef<Interface>>(pointer);
}
struct ObjectLifetime {
    std::atomic<ULONG> references{1};
    ObjectLifetime(){++liveObjects;}
    ~ObjectLifetime(){--liveObjects;}
    ULONG add()noexcept{return ++references;}
};
struct BridgeState {
    std::mutex mutex;
    std::array<BridgeEvent,EventCapacity> events{};
    ULONG begin=0,count=0,connectionId=0;
    ULONGLONG sequence=0,dropped=0,operationId=0;
    bool listening=false,consumed=false,cancelled=false,brokenSent=false,closed=false;
    bool redirectorObserved=false,readyAttempted=false;
    HRESULT brokenResult=E_PENDING,terminalResult=E_PENDING;
    ExperimentOutcome outcome{};
    SharedInterface<IWRdsProtocolListenerCallback> listener;
    SharedInterface<IWRdsProtocolConnectionCallback> connection;
    // StopListen may not call RDS. An actual MTA control worker owns the later
    // cancellation opportunity; preserve its callback until that call finishes.
    bool controlWorkerPresent=false;
    SharedInterface<IWRdsProtocolConnectionCallback> pendingStopConnection;

    HRESULT recordLocked(EventKind kind,HRESULT result=S_OK,const WRDS_SESSION_ID* session=nullptr){
        if(count==EventCapacity){++dropped;return queueFull;}
        BridgeEvent value{};value.sequence=++sequence;value.operationId=operationId;
        value.kind=kind;value.result=result;value.connectionId=connectionId;
        if(session){value.hasSession=TRUE;value.session=*session;}
        events[(begin+count)%EventCapacity]=value;++count;return S_OK;
    }
    HRESULT record(EventKind kind,HRESULT result=S_OK,const WRDS_SESSION_ID* session=nullptr){
        std::lock_guard lock(mutex);return recordLocked(kind,result,session);
    }
};

class Connection final:public IWRdsProtocolConnection {
    ObjectLifetime lifetime_;
    std::shared_ptr<BridgeState> state_;
    HRESULT sessionEvent(EventKind kind,WRDS_SESSION_ID* session)noexcept{
        NotificationScope scope;if(!session)return E_POINTER;
        return boundary([&]{return state_->record(kind,S_OK,session);});
    }
    HRESULT event(EventKind kind,HRESULT result=S_OK)noexcept{
        NotificationScope scope;return boundary([&]{return state_->record(kind,result);});
    }
public:
    explicit Connection(std::shared_ptr<BridgeState> state):state_(std::move(state)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        return activation::invoke("Connection.QueryInterface",[&]()->HRESULT{
        activation::connectionInterface(iid);
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IWRdsProtocolConnection))return E_NOINTERFACE;
        *out=static_cast<IWRdsProtocolConnection*>(this);AddRef();return S_OK;

        });
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return lifetime_.add();}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--lifetime_.references;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE GetLogonErrorRedirector(IWRdsProtocolLogonErrorRedirector** out)override{
        return activation::invoke("Connection.GetLogonErrorRedirector",[&]()->HRESULT{
        NotificationScope scope;if(!out)return E_POINTER;*out=nullptr;
        return boundary([&]()->HRESULT{
            std::lock_guard lock(state_->mutex);
            state_->redirectorObserved=true;
            state_->outcome.logonErrorRedirectorObserved=TRUE;
            (void)state_->recordLocked(EventKind::LogonErrorRedirectorObserved,E_NOTIMPL);
            // Documented opt-out of error redirection. Never call OnReady here.
            return E_NOTIMPL;
        });

        });
    }
    HRESULT STDMETHODCALLTYPE AcceptConnection()override{
        return activation::invoke("Connection.AcceptConnection",[&]()->HRESULT{
        NotificationScope scope;return boundary([&]()->HRESULT{
            std::lock_guard lock(state_->mutex);
            return state_->consumed&&state_->listening&&!state_->cancelled&&!state_->closed?S_OK:HRESULT_FROM_WIN32(ERROR_CANCELLED);
        });

        });
    }
    HRESULT STDMETHODCALLTYPE GetClientData(WRDS_CLIENT_DATA* out)override{
        return activation::invoke("Connection.GetClientData",[&]()->HRESULT{
        if(!out)return E_POINTER;*out={};
        constexpr wchar_t protocol[]=L"RETSAR";
        static_assert(sizeof(protocol)<=sizeof(out->ProtocolName));
        std::copy(std::begin(protocol),std::end(protocol),out->ProtocolName);
        out->fInheritAutoLogon=FALSE;out->fUsingSavedCreds=FALSE;out->fNoAudioPlayback=TRUE;
        return S_OK;

        });
    }
    HRESULT STDMETHODCALLTYPE GetClientMonitorData(UINT* count,UINT* primary)override{
        return activation::invoke("Connection.GetClientMonitorData",[&]()->HRESULT{
        if(count)*count=0;if(primary)*primary=0;return count&&primary?E_NOTIMPL:E_POINTER;

        });
    }
    HRESULT STDMETHODCALLTYPE GetUserCredentials(WRDS_USER_CREDENTIAL* out)override{
        return activation::invoke("Connection.GetUserCredentials",[&]()->HRESULT{
        if(!out)return E_POINTER;*out={};return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE GetLicenseConnection(IWRdsProtocolLicenseConnection** out)override{
        return activation::invoke("Connection.GetLicenseConnection",[&]()->HRESULT{
        if(!out)return E_POINTER;*out=nullptr;return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE AuthenticateClientToSession(WRDS_SESSION_ID* out)override{
        return activation::invoke("Connection.AuthenticateClientToSession",[&]()->HRESULT{
        if(!out)return E_POINTER;*out={};return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE NotifySessionId(WRDS_SESSION_ID* session,HANDLE_PTR)override{
        return activation::invoke("Connection.NotifySessionId",[&]()->HRESULT{
        return sessionEvent(EventKind::SessionAssigned,session);

        });
    }
    HRESULT STDMETHODCALLTYPE GetInputHandles(HANDLE_PTR* keyboard,HANDLE_PTR* mouse,HANDLE_PTR* beep)override{
        return activation::invoke("Connection.GetInputHandles",[&]()->HRESULT{
        if(keyboard)*keyboard=0;if(mouse)*mouse=0;if(beep)*beep=0;
        return keyboard&&mouse&&beep?E_NOTIMPL:E_POINTER;

        });
    }
    HRESULT STDMETHODCALLTYPE GetVideoHandle(HANDLE_PTR* out)override{
        return activation::invoke("Connection.GetVideoHandle",[&]()->HRESULT{
        if(!out)return E_POINTER;*out=0;return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE ConnectNotify(ULONG)override{
        return activation::invoke("Connection.ConnectNotify",[&]()->HRESULT{return event(EventKind::ConnectObserved);
        });
    }
    HRESULT STDMETHODCALLTYPE IsUserAllowedToLogon(ULONG,HANDLE_PTR,WCHAR*,WCHAR*)override{
        return activation::invoke("Connection.IsUserAllowedToLogon",[&]()->HRESULT{return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE SessionArbitrationEnumeration(HANDLE_PTR,BOOL,ULONG*,ULONG* count)override{
        return activation::invoke("Connection.SessionArbitrationEnumeration",[&]()->HRESULT{
        if(!count)return E_POINTER;*count=0;return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE LogonNotify(HANDLE_PTR,WCHAR*,WCHAR*,WRDS_SESSION_ID* session,PWRDS_CONNECTION_SETTINGS)override{
        return activation::invoke("Connection.LogonNotify",[&]()->HRESULT{
        // Account strings and token handles are deliberately not read or stored.
        return sessionEvent(EventKind::LogonObserved,session);

        });
    }
    HRESULT STDMETHODCALLTYPE PreDisconnect(ULONG reason)override{
        return activation::invoke("Connection.PreDisconnect",[&]()->HRESULT{return event(EventKind::PreDisconnectObserved,static_cast<HRESULT>(reason));
        });
    }
    HRESULT STDMETHODCALLTYPE DisconnectNotify()override{
        return activation::invoke("Connection.DisconnectNotify",[&]()->HRESULT{return event(EventKind::DisconnectObserved);
        });
    }
    HRESULT STDMETHODCALLTYPE Close()override{
        return activation::invoke("Connection.Close",[&]()->HRESULT{
        NotificationScope scope;
        return boundary([&]()->HRESULT{
            SharedInterface<IWRdsProtocolConnectionCallback> release,releaseStop;
            HRESULT result;
            {
                std::lock_guard lock(state_->mutex);
                if(state_->closed)return S_OK;
                state_->closed=true;release=std::move(state_->connection);
                releaseStop=std::move(state_->pendingStopConnection);
                result=state_->recordLocked(EventKind::ConnectionClosed);
            }
            // Release only this connection's callback, outside the state lock.
            return result;
        });

        });
    }
    HRESULT STDMETHODCALLTYPE GetProtocolStatus(WRDS_PROTOCOL_STATUS* out)override{
        return activation::invoke("Connection.GetProtocolStatus",[&]()->HRESULT{
        if(!out)return E_POINTER;*out={};return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE GetLastInputTime(ULONG64* out)override{
        return activation::invoke("Connection.GetLastInputTime",[&]()->HRESULT{
        if(!out)return E_POINTER;*out=0;return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE SetErrorInfo(ULONG error)override{
        return activation::invoke("Connection.SetErrorInfo",[&]()->HRESULT{return event(EventKind::ErrorObserved,static_cast<HRESULT>(error));
        });
    }
    HRESULT STDMETHODCALLTYPE CreateVirtualChannel(CHAR*,BOOL,ULONG,ULONG_PTR* out)override{
        return activation::invoke("Connection.CreateVirtualChannel",[&]()->HRESULT{
        if(!out)return E_POINTER;*out=0;return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE QueryProperty(GUID,ULONG,ULONG outCount,PWRDS_PROPERTY_VALUE,PWRDS_PROPERTY_VALUE out)override{
        return activation::invoke("Connection.QueryProperty",[&]()->HRESULT{
        if(outCount&&!out)return E_POINTER;
        if(outCount>256)return E_INVALIDARG;
        for(ULONG i=0;i<outCount;++i)out[i]={};
        return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE GetShadowConnection(IWRdsProtocolShadowConnection** out)override{
        return activation::invoke("Connection.GetShadowConnection",[&]()->HRESULT{
        if(!out)return E_POINTER;*out=nullptr;return E_NOTIMPL;

        });
    }
    HRESULT STDMETHODCALLTYPE NotifyCommandProcessCreated(ULONG)override{
        return activation::invoke("Connection.NotifyCommandProcessCreated",[&]()->HRESULT{return event(EventKind::CommandProcessObserved);
        });
    }
};

class Listener final:public IWRdsProtocolListener,public IRetsarRdsLabControl {
    ObjectLifetime lifetime_;
    std::shared_ptr<BridgeState> state_=std::make_shared<BridgeState>();
    std::mutex ownedMutex_;
    std::shared_ptr<Connection> ownedConnection_;
    // Only explicit laboratory calls acquire this gate, always with try_lock.
    // Notifications never acquire it and never wait for an outbound call.
    std::mutex operationMutex_;

    const std::wstring listenerName_;
    std::mutex controlMutex_;
    std::shared_ptr<lab::Worker> worker_;
    std::shared_ptr<lab::Server> server_;

    void stopServiceControl() noexcept {
        std::shared_ptr<lab::Worker> worker;std::shared_ptr<lab::Server> server;
        {std::lock_guard lock(controlMutex_);worker=std::move(worker_);server=std::move(server_);}
        if(server)server->RequestStop();
        if(worker)worker->RequestStop();
        // No joins or outbound RDS calls on a service notification thread.
    }
    void startServiceControl() noexcept {
        if(listenerName_!=lab::ListenerName)return;
        lab::Config config{};
        const auto enabled=lab::ReadEnabledGuestConfig(config);
        if(enabled!=S_OK){activation::write("LabControl.Gate","return",enabled);return;}
        std::shared_ptr<lab::Worker> worker;std::shared_ptr<lab::Server> server;
        auto result=lab::StartWorker(this,worker);
        if(SUCCEEDED(result)){
            std::lock_guard stateLock(state_->mutex);state_->controlWorkerPresent=true;
        }
        if(SUCCEEDED(result))result=lab::StartServer(config,worker,server);
        activation::write("LabControl.Start","return",result);
        if(FAILED(result)){if(worker)worker->RequestStop();return;}
        bool stop=false;
        {
            std::lock_guard controlLock(controlMutex_);
            std::lock_guard stateLock(state_->mutex);
            stop=!state_->listening||state_->cancelled||state_->closed;
            if(!stop){worker_=worker;server_=server;}
        }
        // A concurrent StopListen must not leave a late-published endpoint.
        if(stop){server->RequestStop();worker->RequestStop();}
    }

    HRESULT cancelOutbound(SharedInterface<IWRdsProtocolConnectionCallback> lateCallback={}){
        SharedInterface<IWRdsProtocolConnectionCallback> callback,releaseStop;
        {
            std::lock_guard lock(state_->mutex);
            state_->cancelled=true;
            state_->outcome.cancellationRequested=TRUE;
            if(state_->brokenSent)return state_->brokenResult;
            if(!state_->closed)callback=lateCallback?lateCallback:
                (state_->connection?state_->connection:state_->pendingStopConnection);
            if(!callback){
                // StopListen may have released a previously accepted callback.
                // Absence of that handle does not prove cancellation succeeded.
                const auto result=state_->outcome.onConnectedAccepted&&!state_->closed?
                    HRESULT_FROM_WIN32(ERROR_NOT_READY):S_OK;
                state_->outcome.cancellationResult=result;return result;
            }
            state_->brokenSent=true;state_->brokenResult=E_PENDING;
        }
        const auto result=callback->pointer->BrokenConnection(0,0);
        {
            std::lock_guard lock(state_->mutex);
            state_->brokenResult=result;state_->outcome.cancellationResult=result;
            (void)state_->recordLocked(EventKind::BrokenConnectionReturned,result);
            releaseStop=std::move(state_->pendingStopConnection);
        }
        // Release the stop-time reservation only after the outgoing callback,
        // outside the state lock (Release itself may invoke COM code).
        return result;
    }
    bool cancellationPending(){
        std::lock_guard lock(state_->mutex);
        return state_->cancelled||!state_->listening||state_->closed;
    }
    HRESULT finish(HRESULT result,ExperimentOutcome* out){
        std::lock_guard lock(state_->mutex);
        if(SUCCEEDED(result)&&(state_->cancelled||!state_->listening||state_->closed))
            result=HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if(result!=E_PENDING)state_->terminalResult=result;
        *out=state_->outcome;return result;
    }
    HRESULT finishCancelled(ExperimentOutcome* out,SharedInterface<IWRdsProtocolConnectionCallback> late={}){
        (void)cancelOutbound(std::move(late));
        return finish(HRESULT_FROM_WIN32(ERROR_CANCELLED),out);
    }
    HRESULT continueOwned(ExperimentOutcome* out){
        // operationMutex_ is held by the explicit caller throughout this method.
        SharedInterface<IWRdsProtocolConnectionCallback> callback;
        bool cancelled=false;
        {
            std::lock_guard lock(state_->mutex);*out=state_->outcome;
            if(!state_->consumed)return HRESULT_FROM_WIN32(ERROR_NOT_READY);
            cancelled=state_->cancelled||!state_->listening||state_->closed;
            if(!cancelled){
                if(state_->terminalResult!=E_PENDING)return state_->terminalResult;
                if(!state_->redirectorObserved)return E_PENDING;
                if(!state_->connection||!state_->outcome.onConnectedAccepted||FAILED(state_->outcome.connectionIdResult))
                    return HRESULT_FROM_WIN32(ERROR_NOT_READY);
                // Admission and cancellation are ordered by this lock. A cancellation
                // after admission returns E_PENDING; it cannot call BrokenConnection
                // until this outbound call returns, and it overrides final success.
                if(state_->readyAttempted)return state_->outcome.onReadyResult;
                state_->readyAttempted=true;callback=state_->connection;
            }
        }
        if(cancelled)return finishCancelled(out);
        const auto result=callback->pointer->OnReady();
        HRESULT recorded;
        {
            std::lock_guard lock(state_->mutex);
            state_->outcome.onReadyResult=result;
            state_->outcome.readyAccepted=SUCCEEDED(result)?TRUE:FALSE;
            recorded=state_->recordLocked(EventKind::OnReadyReturned,result);
        }
        if(cancellationPending())return finishCancelled(out,callback);
        return finish(FAILED(result)?result:recorded,out);
    }
public:
    explicit Listener(const wchar_t* name):listenerName_(name){}
    ~Listener(){
        stopServiceControl();
        // Drop our references even when a laboratory caller omits StopListen.
        // No implicit BrokenConnection or session action is performed here.
        SharedInterface<IWRdsProtocolListenerCallback> listener;
        SharedInterface<IWRdsProtocolConnectionCallback> connection,pendingStop;
        {
            std::lock_guard lock(state_->mutex);
            state_->listening=false;state_->cancelled=true;
            listener=std::move(state_->listener);connection=std::move(state_->connection);
            pendingStop=std::move(state_->pendingStopConnection);
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        return activation::invoke("Listener.QueryInterface",[&]()->HRESULT{
            if(!out)return E_POINTER;*out=nullptr;
            if(iid==__uuidof(IUnknown)||iid==__uuidof(IWRdsProtocolListener))*out=static_cast<IWRdsProtocolListener*>(this);
            else if(iid==__uuidof(IRetsarRdsLabControl))*out=static_cast<IRetsarRdsLabControl*>(this);
            else return E_NOINTERFACE;
            AddRef();return S_OK;
        });
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return lifetime_.add();}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--lifetime_.references;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE GetSettings(WRDS_LISTENER_SETTING_LEVEL,PWRDS_LISTENER_SETTINGS out)override{
        return activation::invoke("Listener.GetSettings",[&]()->HRESULT{
            if(!out)return E_POINTER;*out={};return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE StartListen(IWRdsProtocolListenerCallback* callback)override{
        return activation::invoke("Listener.StartListen",[&]()->HRESULT{
            NotificationScope scope;if(!callback)return E_POINTER;
            const auto apartment=requireMta();if(FAILED(apartment))return apartment;
            return boundary([&]()->HRESULT{
                auto retained=retain(callback);
                {
                    std::lock_guard lock(state_->mutex);
                    if(state_->listening||state_->consumed)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
                    if(state_->cancelled)return HRESULT_FROM_WIN32(ERROR_CANCELLED);
                    const auto recorded=state_->recordLocked(EventKind::Listening);
                    if(FAILED(recorded))return recorded;
                    state_->listener=std::move(retained);state_->listening=true;
                }
                // Explicit owned-guest opt-in only. Startup never requests a
                // connection: only an authenticated controller can enqueue Start.
                startServiceControl();return S_OK;
            });
        });
    }
    HRESULT STDMETHODCALLTYPE StopListen()override{
        return activation::invoke("Listener.StopListen",[&]()->HRESULT{
            NotificationScope scope;
            return boundary([&]()->HRESULT{
                SharedInterface<IWRdsProtocolListenerCallback> listener;
                SharedInterface<IWRdsProtocolConnectionCallback> connection;
                HRESULT recorded=S_OK;
                {
                    std::lock_guard lock(state_->mutex);
                    state_->listening=false;state_->cancelled=true;
                    listener=std::move(state_->listener);
                    if(state_->controlWorkerPresent&&!state_->brokenSent&&!state_->closed&&state_->connection)
                        state_->pendingStopConnection=std::move(state_->connection);
                    else connection=std::move(state_->connection);
                    recorded=state_->recordLocked(EventKind::ListenerStopped);
                }
                stopServiceControl();
                std::shared_ptr<Connection> owned;
                {std::lock_guard lock(ownedMutex_);owned=std::move(ownedConnection_);}
                // No outbound RDS call from this notification and no session cleanup.
                return recorded;
            });
        });
    }
    HRESULT STDMETHODCALLTYPE StartExperiment(BOOL explicitStart,ULONGLONG operation,ExperimentOutcome* out)override{
        if(!out)return E_POINTER;*out={};
        if(explicitStart!=TRUE)return E_ACCESSDENIED;
        if(!operation)return E_INVALIDARG;
        if(notificationDepth||controlDepth)return RPC_E_CALL_REJECTED;
        const auto apartment=requireMta();if(FAILED(apartment))return apartment;
        return boundary([&]()->HRESULT{
            std::unique_lock outbound(operationMutex_,std::try_to_lock);
            if(!outbound.owns_lock())return E_PENDING;
            ControlScope control;
            SharedInterface<IWRdsProtocolListenerCallback> callback;
            {
                std::lock_guard lock(state_->mutex);
                if(state_->consumed)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
                if(!state_->listening||!state_->listener||state_->cancelled)return HRESULT_FROM_WIN32(ERROR_NOT_READY);
                state_->operationId=operation;state_->outcome.operationId=operation;
                const auto recorded=state_->recordLocked(EventKind::ExperimentStarted);
                if(FAILED(recorded))return recorded;
                state_->consumed=true;callback=state_->listener;out->operationId=operation;
            }
            auto connection=std::shared_ptr<Connection>(new Connection(state_),[](Connection* value){value->Release();});
            {std::lock_guard lock(ownedMutex_);if(!cancellationPending())ownedConnection_=connection;}
            WRDS_CONNECTION_SETTINGS settings{};
            settings.WRdsConnectionSettingLevel=WRDS_CONNECTION_SETTING_LEVEL_1;
            auto& listenerSettings=settings.WRdsConnectionSetting.WRdsConnectionSettings1.WRdsListenerSettings;
            listenerSettings.WRdsListenerSettingLevel=WRDS_LISTENER_SETTING_LEVEL_1;
            listenerSettings.WRdsListenerSetting.WRdsListenerSettings1.MaxProtocolListenerConnectionCount=1;
            // No supplied security descriptor or credentials. These minimal
            // settings are mock-tested only; real transport/security negotiation
            // remains unsupported and is not claimed to inherit OS defaults.
            if(cancellationPending())return finishCancelled(out);
            IWRdsProtocolConnectionCallback* returned=nullptr;
            const auto connectedResult=callback->pointer->OnConnected(connection.get(),&settings,&returned);
            SharedInterface<IWRdsProtocolConnectionCallback> retained;
            if(returned){
                try{retained=retain(returned);}catch(...){returned->Release();throw;}
                returned->Release();
            }
            HRESULT connectedRecorded;
            {
                std::lock_guard lock(state_->mutex);
                state_->outcome.onConnectedResult=connectedResult;
                state_->outcome.onConnectedAccepted=SUCCEEDED(connectedResult)?TRUE:FALSE;
                connectedRecorded=state_->recordLocked(EventKind::OnConnectedReturned,connectedResult);
                // A late result after StopListen is only retained in this stack
                // frame. It must not resurrect the listener's callback.
                if(SUCCEEDED(connectedResult)&&state_->listening&&!state_->cancelled&&!state_->closed)
                    state_->connection=retained;
            }
            if(cancellationPending())return finishCancelled(out,retained);
            if(FAILED(connectedResult))return finish(connectedResult,out);
            if(!retained)return finish(E_UNEXPECTED,out);
            if(FAILED(connectedRecorded))return finish(connectedRecorded,out);
            ULONG connectionId=0;
            const auto idResult=retained->pointer->GetConnectionId(&connectionId);
            HRESULT idRecorded;
            {
                std::lock_guard lock(state_->mutex);
                state_->outcome.connectionIdResult=idResult;
                if(SUCCEEDED(idResult)){state_->connectionId=connectionId;state_->outcome.connectionId=connectionId;}
                idRecorded=state_->recordLocked(EventKind::ConnectionIdObserved,idResult);
            }
            if(cancellationPending())return finishCancelled(out,retained);
            if(FAILED(idResult))return finish(idResult,out);
            if(FAILED(idRecorded))return finish(idRecorded,out);
            return continueOwned(out);
        });
    }
    HRESULT STDMETHODCALLTYPE ContinueExperiment(ExperimentOutcome* out)override{
        if(!out)return E_POINTER;*out={};
        if(notificationDepth||controlDepth)return RPC_E_CALL_REJECTED;
        const auto apartment=requireMta();if(FAILED(apartment))return apartment;
        return boundary([&]()->HRESULT{
            std::unique_lock outbound(operationMutex_,std::try_to_lock);
            if(!outbound.owns_lock())return E_PENDING;
            ControlScope control;return continueOwned(out);
        });
    }
    HRESULT STDMETHODCALLTYPE CancelExperiment()override{
        if(notificationDepth||controlDepth)return RPC_E_CALL_REJECTED;
        const auto apartment=requireMta();if(FAILED(apartment))return apartment;
        return boundary([&]()->HRESULT{
            {
                std::lock_guard lock(state_->mutex);
                if(!state_->cancelled)(void)state_->recordLocked(EventKind::CancellationRequested);
                state_->cancelled=true;state_->outcome.cancellationRequested=TRUE;
            }
            std::unique_lock outbound(operationMutex_,std::try_to_lock);
            if(!outbound.owns_lock())return E_PENDING;
            ControlScope control;return cancelOutbound();
        });
    }
    HRESULT STDMETHODCALLTYPE DrainEvents(BridgeEvent* events,ULONG capacity,ULONG* written,ULONGLONG* dropped)override{
        if(written)*written=0;if(dropped)*dropped=0;
        if(!written||!dropped||(capacity&&!events))return E_POINTER;
        return boundary([&]()->HRESULT{
            std::lock_guard lock(state_->mutex);*dropped=state_->dropped;
            const auto count=std::min(capacity,state_->count);
            for(ULONG i=0;i<count;++i)events[i]=state_->events[(state_->begin+i)%EventCapacity];
            state_->begin=(state_->begin+count)%EventCapacity;state_->count-=count;*written=count;return S_OK;
        });
    }
};

class Manager final:public IWRdsProtocolManager {
    ObjectLifetime lifetime_;
    std::mutex mutex_;
    SharedInterface<IWRdsProtocolSettings> settings_;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        return activation::invoke("Manager.QueryInterface",[&]()->HRESULT{
            if(!out)return E_POINTER;*out=nullptr;
            if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IWRdsProtocolManager))return E_NOINTERFACE;
            *out=static_cast<IWRdsProtocolManager*>(this);AddRef();return S_OK;
        });
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return lifetime_.add();}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--lifetime_.references;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE Initialize(IWRdsProtocolSettings* settings,PWRDS_SETTINGS values)override{
        return activation::invoke("Manager.Initialize",[&]()->HRESULT{
            NotificationScope scope;if(!values)return E_POINTER;
            return boundary([&]()->HRESULT{
                // The SDK marks pIWRdsSettings optional; pWRdsSettings is required.
                auto retained=settings?retain(settings):SharedInterface<IWRdsProtocolSettings>{};
                SharedInterface<IWRdsProtocolSettings> previous;
                {std::lock_guard lock(mutex_);previous=std::move(settings_);settings_=std::move(retained);}return S_OK;
            });
        });
    }
    HRESULT STDMETHODCALLTYPE CreateListener(WCHAR* name,IWRdsProtocolListener** out)override{
        return activation::invoke("Manager.CreateListener",[&]()->HRESULT{
            NotificationScope scope;if(!out)return E_POINTER;*out=nullptr;
            if(!name||!*name)return E_INVALIDARG;
            return boundary([&]()->HRESULT{*out=new Listener(name);return S_OK;});
        });
    }
    HRESULT STDMETHODCALLTYPE NotifyServiceStateChange(WRDS_SERVICE_STATE*)override{
        return activation::invoke("Manager.NotifyServiceStateChange",[&]()->HRESULT{
            return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE NotifySessionOfServiceStart(WRDS_SESSION_ID*)override{
        return activation::invoke("Manager.NotifySessionOfServiceStart",[&]()->HRESULT{
            return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE NotifySessionOfServiceStop(WRDS_SESSION_ID*)override{
        return activation::invoke("Manager.NotifySessionOfServiceStop",[&]()->HRESULT{
            return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE NotifySessionStateChange(WRDS_SESSION_ID*,ULONG)override{
        return activation::invoke("Manager.NotifySessionStateChange",[&]()->HRESULT{
            return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE NotifySettingsChange(PWRDS_SETTINGS)override{
        return activation::invoke("Manager.NotifySettingsChange",[&]()->HRESULT{
            return E_NOTIMPL;
        });
    }
    HRESULT STDMETHODCALLTYPE Uninitialize()override{
        return activation::invoke("Manager.Uninitialize",[&]()->HRESULT{
            NotificationScope scope;return boundary([&]()->HRESULT{
                SharedInterface<IWRdsProtocolSettings> release;
                {std::lock_guard lock(mutex_);release=std::move(settings_);}return S_OK;
            });
        });
    }
};

class Factory final:public IClassFactory {
    ObjectLifetime lifetime_;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        return activation::invoke("Factory.QueryInterface",[&]()->HRESULT{
            if(!out)return E_POINTER;*out=nullptr;
            if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IClassFactory))return E_NOINTERFACE;
            *out=static_cast<IClassFactory*>(this);AddRef();return S_OK;
        });
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return lifetime_.add();}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--lifetime_.references;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out)override{
        return activation::invoke("Factory.CreateInstance",[&]()->HRESULT{
            if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;
            return boundary([&]()->HRESULT{
                auto manager=new Manager();const auto result=manager->QueryInterface(iid,out);manager->Release();return result;
            });
        });
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock)override{
        return activation::invoke("Factory.LockServer",[&]()->HRESULT{
            if(lock){++serverLocks;return S_OK;}
            auto observed=serverLocks.load();
            while(observed){if(serverLocks.compare_exchange_weak(observed,observed-1))return S_OK;}
            return E_UNEXPECTED;
        });
    }
};
}
}

STDAPI DllGetClassObject(REFCLSID clsid,REFIID iid,void** out){
    return seats::rds::activation::invoke("DllGetClassObject",[&]()->HRESULT{
        using namespace seats::rds;
        if(!out)return E_POINTER;*out=nullptr;
        if(clsid!=NativeRdsBridgeClass)return CLASS_E_CLASSNOTAVAILABLE;
        return boundary([&]()->HRESULT{
            auto factory=new Factory();const auto result=factory->QueryInterface(iid,out);factory->Release();return result;
        });
    });
}
STDAPI DllCanUnloadNow(){
    return seats::rds::activation::invoke("DllCanUnloadNow",[&]()->HRESULT{
        using namespace seats::rds;return liveObjects.load()==0&&serverLocks.load()==0?S_OK:S_FALSE;
    });
}

BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH)seats::rds::activation::processAttach();
    else if(reason==DLL_PROCESS_DETACH)seats::rds::activation::processDetach();
    return TRUE;
}
