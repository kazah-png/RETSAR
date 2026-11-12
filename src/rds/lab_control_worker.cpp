#include "lab_control_worker.hpp"
#include "activation_trace.hpp"
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <new>

namespace seats::rds::lab {
namespace {
constexpr DWORD LeaseMs=120000;
class WorkerImpl final : public Worker {
    std::mutex mutex_;
    HANDLE wake_=nullptr;
    IRetsarRdsLabControl* control_=nullptr; // Released only by the MTA worker.
    Response response_{};
    ULONGLONG bridgeDropped_=0,localDropped_=0;
    bool admitted_=false,startQueued_=false,continueQueued_=false,cancelQueued_=false;
    std::atomic<bool> stop_{false},stopped_{false};
    void collect() noexcept {
        BridgeEvent events[EventCapacity]{};ULONG count=0;ULONGLONG dropped=0;
        const auto result=control_->DrainEvents(events,EventCapacity,&count,&dropped);
        if(FAILED(result)||count>EventCapacity)return;
        std::lock_guard lock(mutex_);
        bridgeDropped_=std::max(bridgeDropped_,dropped);
        const auto room=EventCapacity-response_.eventCount;
        const auto copied=std::min(room,count);
        for(ULONG i=0;i<copied;++i)response_.events[response_.eventCount++]=events[i];
        localDropped_+=count-copied;
        response_.droppedEvents=bridgeDropped_+localDropped_;
    }
public:
    explicit WorkerImpl(HANDLE wake):wake_(wake){}
    ~WorkerImpl() override { if(wake_)CloseHandle(wake_); }
    void setControl(IRetsarRdsLabControl* value) noexcept {control_=value;}
    void RequestStop() noexcept override {stop_=true;SetEvent(wake_);}
    bool Stopped() const noexcept override {return stopped_.load();}
    Response Handle(const Request& request) noexcept override {
        try{
            std::lock_guard lock(mutex_);
            if(stop_||stopped_){auto out=response_;out.state=State::Stopping;out.result=HRESULT_FROM_WIN32(ERROR_CANCELLED);return out;}
            if(request.command==Command::Snapshot)return response_;
            if(!request.operationId){auto out=response_;out.result=E_INVALIDARG;return out;}
            if(admitted_&&response_.outcome.operationId!=request.operationId){auto out=response_;out.result=HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);return out;}
            if(request.command==Command::Start){
                if(admitted_)return response_; // Idempotent, including completed attempts.
                admitted_=true;response_.outcome.operationId=request.operationId;
                response_.state=State::Queued;response_.result=E_PENDING;startQueued_=true;
            }else if(!admitted_){auto out=response_;out.result=HRESULT_FROM_WIN32(ERROR_NOT_READY);return out;
            }else if(request.command==Command::Cancel){
                cancelQueued_=true;response_.outcome.cancellationRequested=TRUE;
            }else if(request.command==Command::Continue){
                if(response_.state==State::Pending)continueQueued_=true;
            }else {auto out=response_;out.result=E_INVALIDARG;return out;}
            SetEvent(wake_);return response_;
        }catch(...){Response out{};out.result=E_FAIL;return out;}
    }
    void run() noexcept {
        const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        activation::write("LabWorker.Mta","return",init);
        if(SUCCEEDED(init)){
            const auto deadline=GetTickCount64()+LeaseMs;
            bool pending=false,everStarted=false;
            while(!stop_ && GetTickCount64()<deadline){
                bool start=false,cont=false,cancel=false;ULONGLONG operation=0;
                {
                    std::lock_guard lock(mutex_);
                    start=startQueued_;startQueued_=false;
                    cont=continueQueued_||pending;continueQueued_=false;
                    cancel=cancelQueued_;cancelQueued_=false;
                    operation=response_.outcome.operationId;
                    if(start||cont||cancel)response_.state=State::Working;
                }
                HRESULT result=E_PENDING;ExperimentOutcome outcome{};
                if(cancel){
                    result=control_->CancelExperiment();pending=false;
                    std::lock_guard lock(mutex_);outcome=response_.outcome;
                    outcome.cancellationRequested=TRUE;outcome.cancellationResult=result;
                }else if(start){
                    everStarted=true;
                    result=control_->StartExperiment(TRUE,operation,&outcome);
                    activation::write("LabWorker.Start","return",result);
                    pending=result==E_PENDING;
                }else if(cont){
                    result=control_->ContinueExperiment(&outcome);pending=result==E_PENDING;
                }
                if(start||cont||cancel){
                    std::lock_guard lock(mutex_);
                    // Never replace the operation ID with an uninitialized output.
                    if(!outcome.operationId)outcome.operationId=operation;
                    response_.outcome=outcome;response_.result=result;
                    response_.state=pending?State::Pending:State::Finished;
                }
                collect();
                if(stop_)break;
                // Poll only after a real Start requested by the controller. This
                // observes readiness on a separate MTA thread, outside the notification stack.
                WaitForSingleObject(wake_,pending?100:250);
            }
            if(everStarted){
                const auto cancelled=control_->CancelExperiment();
                activation::write("LabWorker.LeaseOrStopCancel","return",cancelled);
                collect();
                std::lock_guard lock(mutex_);response_.outcome.cancellationRequested=TRUE;
                response_.outcome.cancellationResult=cancelled;
            }
            control_->Release();control_=nullptr;CoUninitialize();
        }else{
            // AddRef/Release are allowed on an uninitialized COM thread; no method
            // is invoked on this failure path. The SDK object is Free-threaded.
            control_->Release();control_=nullptr;
            std::lock_guard lock(mutex_);response_.result=init;
        }
        {std::lock_guard lock(mutex_);response_.state=State::Stopping;}
        stopped_=true;
    }
};
struct ThreadState {std::shared_ptr<WorkerImpl> worker;HMODULE module=nullptr;};
DWORD WINAPI entry(void* context) noexcept {
    auto data=static_cast<ThreadState*>(context);HMODULE module=data->module;
    {auto worker=std::move(data->worker);delete data;worker->run();}
    // No C++ object referring to this DLL remains on the worker stack.
    FreeLibraryAndExitThread(module,0);
}
}
HRESULT StartWorker(IRetsarRdsLabControl* control,std::shared_ptr<Worker>& out) noexcept {
    out.reset();if(!control)return E_POINTER;
    HMODULE module=nullptr;HANDLE wake=nullptr;
    try{
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&StartWorker),&module))return HRESULT_FROM_WIN32(GetLastError());
        wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!wake){const auto error=GetLastError();FreeLibrary(module);return HRESULT_FROM_WIN32(error);}
        auto worker=std::make_shared<WorkerImpl>(wake);wake=nullptr;
        auto state=std::make_unique<ThreadState>();state->worker=worker;state->module=module;
        control->AddRef();worker->setControl(control);
        const auto thread=CreateThread(nullptr,0,entry,state.get(),0,nullptr);
        if(!thread){
            const auto error=GetLastError();worker->setControl(nullptr);control->Release();
            state.reset();worker.reset();FreeLibrary(module);return HRESULT_FROM_WIN32(error);
        }
        state.release();CloseHandle(thread);out=std::move(worker);return S_OK;
    }catch(...){
        if(wake)CloseHandle(wake);if(module)FreeLibrary(module);return E_OUTOFMEMORY;
    }
}
}
