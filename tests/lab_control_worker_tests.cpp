#include "../src/rds/lab_control_worker.hpp"
#ifdef RETSAR_WORKER_TEST_DLL
#include "../src/rds/activation_trace.hpp"
// The fixture links only the real worker implementation. It does not contain
// a protocol provider, service, pipe server, registration, or activation ETW.
namespace seats::rds::activation { void write(const char*,const char*,HRESULT)noexcept{} }
extern "C" __declspec(dllexport) HRESULT WINAPI StartMockWorker(
    seats::rds::IRetsarRdsLabControl* control,std::shared_ptr<seats::rds::lab::Worker>* worker){
    if(!worker)return E_POINTER;
    return seats::rds::lab::StartWorker(control,*worker);
}
#else
#include <objbase.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <vector>
using namespace seats::rds;
using namespace seats::rds::lab;
namespace {
int failures=0,checks=0;
void check(bool pass,const char* text){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
bool waitFor(const std::function<bool()>& predicate,DWORD milliseconds=3000){
    const auto end=GetTickCount64()+milliseconds;
    do{if(predicate())return true;Sleep(2);}while(GetTickCount64()<end);
    return predicate();
}
bool isMta(){APTTYPE apartment{};APTTYPEQUALIFIER qualifier{};return SUCCEEDED(CoGetApartmentType(&apartment,&qualifier))&&apartment==APTTYPE_MTA;}
struct Evidence {
    std::atomic<ULONG> start{0},cont{0},cancel{0},drain{0},mtaFailures{0},references{1};
    std::atomic<bool> workerReleased{false},workerReleaseMta{false};
};
class MockControl final:public IRetsarRdsLabControl {
    std::shared_ptr<Evidence> evidence_;
    std::mutex mutex_;
    std::vector<BridgeEvent> events_;
    std::atomic<ULONGLONG> operation_{0},dropped_{0};
public:
    HANDLE startGate=CreateEventW(nullptr,TRUE,TRUE,nullptr);
    HANDLE continueGate=CreateEventW(nullptr,TRUE,TRUE,nullptr);
    std::atomic<HRESULT> startResult{S_OK},continueResult{S_OK},cancelResult{S_OK};
    explicit MockControl(std::shared_ptr<Evidence> evidence):evidence_(std::move(evidence)){}
    ~MockControl(){CloseHandle(startGate);CloseHandle(continueGate);}
    void observe(){if(!isMta())++evidence_->mtaFailures;}
    void queue(ULONG count,ULONGLONG dropped){
        std::lock_guard lock(mutex_);dropped_=dropped;
        for(ULONG i=0;i<count;++i){BridgeEvent event{};event.sequence=i+1;event.kind=EventKind::ErrorObserved;events_.push_back(event);}
    }
    void outcome(ExperimentOutcome* out){
        *out={};out->operationId=operation_;out->onConnectedResult=startResult;
        out->onConnectedAccepted=SUCCEEDED(startResult)?TRUE:FALSE;
        out->onReadyResult=continueResult;out->readyAccepted=continueResult==S_OK?TRUE:FALSE;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IRetsarRdsLabControl))return E_NOINTERFACE;
        *out=static_cast<IRetsarRdsLabControl*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++evidence_->references;}
    ULONG STDMETHODCALLTYPE Release()override{
        const auto value=--evidence_->references;
        if(value==1){evidence_->workerReleaseMta=isMta();evidence_->workerReleased=true;}
        if(!value)delete this;return value;
    }
    HRESULT STDMETHODCALLTYPE StartExperiment(BOOL explicitStart,ULONGLONG operation,ExperimentOutcome* out)override{
        observe();++evidence_->start;operation_=operation;
        if(explicitStart!=TRUE||!operation||!out)return E_INVALIDARG;
        if(WaitForSingleObject(startGate,10000)!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        outcome(out);return startResult;
    }
    HRESULT STDMETHODCALLTYPE ContinueExperiment(ExperimentOutcome* out)override{
        observe();++evidence_->cont;
        if(!out)return E_POINTER;
        if(WaitForSingleObject(continueGate,10000)!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        outcome(out);return continueResult;
    }
    HRESULT STDMETHODCALLTYPE CancelExperiment()override{observe();++evidence_->cancel;return cancelResult;}
    HRESULT STDMETHODCALLTYPE DrainEvents(BridgeEvent* events,ULONG capacity,ULONG* written,ULONGLONG* dropped)override{
        observe();std::lock_guard lock(mutex_);++evidence_->drain;
        if(!written||!dropped||(!events&&capacity))return E_POINTER;
        *written=static_cast<ULONG>(std::min<size_t>(capacity,events_.size()));*dropped=dropped_;
        for(ULONG i=0;i<*written;++i)events[i]=events_[i];
        events_.erase(events_.begin(),events_.begin()+*written);return S_OK;
    }
};
using Start=HRESULT(WINAPI*)(IRetsarRdsLabControl*,std::shared_ptr<Worker>*);
Request request(Command command,ULONGLONG operation=1){Request value{};value.command=command;value.operationId=operation;return value;}
struct Fixture {
    std::shared_ptr<Evidence> evidence=std::make_shared<Evidence>();
    MockControl* control=new MockControl(evidence);
    std::shared_ptr<Worker> worker;
    explicit Fixture(Start start){check(start(control,&worker)==S_OK&&worker!=nullptr,"real MTA worker starts against mock control");}
    Response snapshot(){return worker->Handle(request(Command::Snapshot));}
    void stop(){
        SetEvent(control->startGate);SetEvent(control->continueGate);worker->RequestStop();
        check(waitFor([&]{return worker->Stopped();}),"worker completes cooperative stop");
    }
    ~Fixture(){
        if(worker){SetEvent(control->startGate);SetEvent(control->continueGate);worker->RequestStop();
            if(!waitFor([&]{return worker->Stopped();},5000))ExitProcess(10);
            worker.reset();}
        control->Release();
    }
};
void passive(Start start){
    Fixture f(start);
    check(waitFor([&]{return f.evidence->drain.load()>0;}),"worker has entered its loop");
    const auto snapshot=f.snapshot();
    check(f.evidence->start==0&&f.evidence->cont==0&&f.evidence->cancel==0,"startup and snapshot never call connection methods");
    check(snapshot.state==State::Idle&&snapshot.result==E_PENDING&&snapshot.outcome.operationId==0,"idle snapshot does not claim a result");
    check(f.worker->Handle(request(Command::Continue)).result==HRESULT_FROM_WIN32(ERROR_NOT_READY),"continue before start rejected");
    check(f.worker->Handle(request(Command::Cancel)).result==HRESULT_FROM_WIN32(ERROR_NOT_READY),"cancel before start rejected");
    check(f.worker->Handle(request(Command::Start,0)).result==E_INVALIDARG,"zero operation rejected");
    f.stop();
    check(f.evidence->cancel==0,"stopping an idle worker does not invent a cancellation");
    check(f.evidence->workerReleased&&f.evidence->workerReleaseMta&&f.evidence->references==1,"worker releases exactly its retained control reference on MTA");
    check(f.evidence->mtaFailures==0,"passive control observations run on MTA");
}
void operation(Start start){
    Fixture f(start);f.control->startResult=E_ACCESSDENIED;
    (void)f.worker->Handle(request(Command::Start,41));
    check(waitFor([&]{return f.snapshot().state==State::Finished;}),"explicit start finishes");
    const auto result=f.snapshot();
    check(result.result==E_ACCESSDENIED&&result.outcome.onConnectedResult==E_ACCESSDENIED,"start failure remains observable");
    for(int i=0;i<8;++i)(void)f.worker->Handle(request(Command::Start,41));
    check(f.evidence->start==1,"repeated start with same ID never repeats a connection");
    check(f.worker->Handle(request(Command::Start,42)).result==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),"a second operation is rejected");
    check(f.worker->Handle(request(Command::Cancel,42)).result==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),"wrong operation cannot cancel the admitted operation");
    f.control->cancelResult=HRESULT_FROM_WIN32(ERROR_BUSY);
    (void)f.worker->Handle(request(Command::Cancel,41));
    check(waitFor([&]{return f.snapshot().outcome.cancellationResult==HRESULT_FROM_WIN32(ERROR_BUSY);}),"explicit cancellation result is retained");
    check(f.snapshot().outcome.onConnectedResult==E_ACCESSDENIED&&!f.snapshot().outcome.sessionOwnershipProven,"cancellation does not erase original connection failure or claim a session");
    f.stop();check(f.evidence->mtaFailures==0,"start/cancel/drain/release all run on MTA");
}
void continuation(Start start){
    Fixture f(start);f.control->startResult=E_PENDING;f.control->continueResult=E_PENDING;
    (void)f.worker->Handle(request(Command::Start,9));
    check(waitFor([&]{return f.evidence->cont.load()>0;}),"pending start is continued from independent MTA worker");
    f.control->continueResult=E_NOTIMPL;
    check(waitFor([&]{return f.snapshot().state==State::Finished;}),"terminal continuation stops polling");
    const auto terminal=f.snapshot();
    check(terminal.result==E_NOTIMPL&&terminal.outcome.onReadyResult==E_NOTIMPL,"continuation HRESULT remains observable");
    const auto calls=f.evidence->cont.load();
    (void)f.worker->Handle(request(Command::Continue,9));
    check(f.evidence->cont==calls,"completed operation cannot be reopened by continue");
    f.stop();check(f.evidence->mtaFailures==0,"continuation never executes on handler thread");
}
void blocked(Start start){
    Fixture f(start);ResetEvent(f.control->startGate);
    (void)f.worker->Handle(request(Command::Start,17));
    check(waitFor([&]{return f.evidence->start.load()==1;}),"mock outbound start is blocked");
    const auto before=GetTickCount64();const auto snapshot=f.snapshot();f.worker->RequestStop();
    check(GetTickCount64()-before<200,"snapshot and stop do not wait for blocked outbound call");
    check(snapshot.state==State::Working&&!f.worker->Stopped(),"blocked callback is not falsely reported as a stopped worker");
    check(f.evidence->references==2&&!f.evidence->workerReleased,"blocked callback retains its control reference");
    const auto rejected=f.worker->Handle(request(Command::Start,18));
    check(rejected.state==State::Stopping&&rejected.result==HRESULT_FROM_WIN32(ERROR_CANCELLED),"stop revokes new work immediately");
    SetEvent(f.control->startGate);
    check(waitFor([&]{return f.worker->Stopped();}),"worker stops after callback returns");
    check(f.evidence->cancel==1&&f.evidence->cont==0,"stop cancels on MTA after return without a late continuation");
    check(f.evidence->workerReleased&&f.evidence->workerReleaseMta,"late callback cleanup releases on MTA");
}
void overflow(Start start){
    Fixture f(start);f.control->queue(EventCapacity,3);
    check(waitFor([&]{return f.snapshot().eventCount==EventCapacity;}),"bounded response receives complete first event batch");
    check(f.snapshot().droppedEvents==3,"provider loss reported independently");
    f.control->queue(5,10);
    check(waitFor([&]{return f.snapshot().droppedEvents==15;}),"cumulative provider loss and local overflow are added");
    const auto batch=f.evidence->drain.load();
    check(waitFor([&]{return f.evidence->drain.load()>batch;}),"empty event batch processed");
    check(f.snapshot().droppedEvents==15&&f.snapshot().eventCount==EventCapacity,"empty drains do not recount loss or overflow storage");
    f.control->queue(2,12);
    check(waitFor([&]{return f.snapshot().droppedEvents==19;}),"later cumulative loss does not mask prior local loss");
    f.stop();
}
void pin(Start start,HMODULE& library,const wchar_t* path){
    auto evidence=std::make_shared<Evidence>();auto control=new MockControl(evidence);
    std::shared_ptr<Worker> worker;ResetEvent(control->startGate);
    check(start(control,&worker)==S_OK,"pin fixture worker created");
    (void)worker->Handle(request(Command::Start,23));
    check(waitFor([&]{return evidence->start.load()==1;}),"pin fixture blocks in actual worker thread");
    check(FreeLibrary(library)!=FALSE,"test owner releases its library reference");library=nullptr;
    check(GetModuleHandleW(path)!=nullptr,"worker pin retains DLL during outstanding callback");
    worker->RequestStop();worker.reset();
    check(GetModuleHandleW(path)!=nullptr&&!evidence->workerReleased,"dropping external worker does not unload outstanding code");
    SetEvent(control->startGate);
    check(waitFor([&]{return evidence->workerReleased.load();}),"pinned worker releases mock control after callback");
    check(waitFor([&]{return GetModuleHandleW(path)==nullptr;}),"DLL pin is released only after worker epilogue exits");
    check(evidence->cancel==1&&evidence->mtaFailures==0,"pinned stop performs one MTA cancellation");
    control->Release();
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2){std::cerr<<"Pass the inert worker fixture DLL path.\n";return 2;}
    HMODULE library=LoadLibraryW(argv[1]);if(!library){std::cerr<<"Cannot load fixture DLL.\n";return 2;}
    const auto start=reinterpret_cast<Start>(GetProcAddress(library,"StartMockWorker"));
    if(!start){FreeLibrary(library);return 2;}
    std::shared_ptr<Worker> absent;check(start(nullptr,&absent)==E_POINTER&&!absent,"null control rejected");
    passive(start);operation(start);continuation(start);blocked(start);overflow(start);pin(start,library,argv[1]);
    if(library)FreeLibrary(library);
    std::cout<<"Laboratory worker: "<<checks<<" checks, "<<failures<<" failures. Mock COM control; no service, connection, or session.\n";
    return failures?1:0;
}
#endif
