#include "child_session_experiment_io.hpp"
#include "child_session_activex.hpp"
#include "../json.hpp"
#include <taskschd.h>
#include <sddl.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <thread>
#include <iostream>

namespace {
using namespace seats::lab::child;
using namespace seats::lab::child::experiment;
using seats::lab::child::ax::Com;
using seats::lab::child::ax::Variant;
constexpr wchar_t ProbePath[]=L"D:\\RETSAR-Lab\\RETSAR\\dist\\lab\\RETSAR-child-session-input-probe.exe";
constexpr wchar_t ProbeDirectory[]=L"D:\\RETSAR-Lab\\RETSAR\\dist\\lab";
constexpr ULONGLONG DeadlineMs=45000;
struct Bstr {
    BSTR value=nullptr;
    Bstr()=default;explicit Bstr(const wchar_t* text):value(SysAllocString(text)){}
    ~Bstr(){SysFreeString(value);}
    Bstr(const Bstr&)=delete;Bstr& operator=(const Bstr&)=delete;
    BSTR* put(){SysFreeString(value);value=nullptr;return &value;}
};
bool SessionNumber(const wchar_t* text,DWORD& value)noexcept {
    if(!text||!*text)return false;
    ULONGLONG number=0;
    for(const wchar_t* p=text;*p;++p) {
        if(*p<L'0'||*p>L'9')return false;
        number=number*10+static_cast<unsigned>(*p-L'0');
        if(number>0x7fffffff)return false;
    }
    if(number==0)return false;value=static_cast<DWORD>(number);return true;
}
std::wstring QuoteArgument(const std::wstring& input) {
    std::wstring out=L"\"";std::size_t slashes=0;
    for(wchar_t c:input) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'"'){out.append(slashes*2+1,L'\\');out+=c;}
        else {out.append(slashes,L'\\');out+=c;}
        slashes=0;
    }
    out.append(slashes*2,L'\\');out+=L'"';return out;
}
HRESULT Hash(const BYTE* bytes,ULONG count,std::string& output) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    NTSTATUS status=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    if(status<0)return HRESULT_FROM_NT(status);
    status=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    if(status>=0)status=BCryptHashData(hash,const_cast<BYTE*>(bytes),count,0);
    BYTE digest[32]{};
    if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)return HRESULT_FROM_NT(status);
    constexpr char hex[]="0123456789abcdef";output.clear();output.reserve(64);
    for(BYTE b:digest){output+=hex[b>>4];output+=hex[b&15];}return S_OK;
}
bool ExactDiskHandle(HANDLE handle,const std::wstring& path) {
    BY_HANDLE_FILE_INFORMATION info{};wchar_t resolved[32768]{};
    const DWORD length=GetFinalPathNameByHandleW(handle,resolved,static_cast<DWORD>(std::size(resolved)),
        FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto expected=L"\\\\?\\"+path;
    return GetFileType(handle)==FILE_TYPE_DISK&&GetFileInformationByHandle(handle,&info)&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        length>0&&length<std::size(resolved)&&_wcsicmp(expected.c_str(),resolved)==0;
}
bool FixedProbe(HANDLE& output,std::string& hash) {
    const std::wstring path=ProbePath;
    for(std::size_t i=3;i<path.size();++i)if(path[i]==L'\\') {
        const DWORD attrs=GetFileAttributesW(path.substr(0,i).c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES||!(attrs&FILE_ATTRIBUTE_DIRECTORY)||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    output=CreateFileW(ProbePath,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(output==INVALID_HANDLE_VALUE||!ExactDiskHandle(output,path))return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(output,&size)||size.QuadPart<=0||size.QuadPart>16*1024*1024)return false;
    std::vector<BYTE> data(static_cast<std::size_t>(size.QuadPart));DWORD read=0;
    if(!ReadFile(output,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)||read!=data.size())return false;
    return SUCCEEDED(Hash(data.data(),read,hash));
}
bool BoundSessions(DWORD expectedChild,DWORD expectedParent,Snapshot& snapshot) {
    snapshot=Query(SystemApi());
    if(!snapshot.enabled.succeeded||!snapshot.enabled.value||!snapshot.caller.succeeded||
       snapshot.caller.value==0||snapshot.caller.value==MAXDWORD||
       snapshot.console.value!=snapshot.caller.value||
       (expectedParent&&snapshot.caller.value!=expectedParent)||
       !snapshot.child.succeeded||snapshot.child.value!=expectedChild||expectedChild==snapshot.caller.value)return false;
    PWTS_SESSION_INFOW rows=nullptr;DWORD count=0;
    if(!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&rows,&count))return false;
    bool parent=false,child=false;
    if(rows&&count<=1024)for(DWORD i=0;i<count;++i) {
        if(rows[i].SessionId==snapshot.caller.value&&rows[i].State==WTSActive)parent=true;
        if(rows[i].SessionId==expectedChild&&rows[i].State==WTSActive)child=true;
    }
    if(rows)WTSFreeMemory(rows);return parent&&child;
}
bool CurrentSid(std::wstring& value) {
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))return false;
    DWORD size=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&size);
    if(size<sizeof(TOKEN_USER)||size>65536)return false;
    std::vector<BYTE> storage(size);
    if(!GetTokenInformation(token.value,TokenUser,storage.data(),size,&size))return false;
    auto* user=reinterpret_cast<TOKEN_USER*>(storage.data());LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(user->User.Sid,&sid))return false;
    value=sid;LocalFree(sid);return true;
}
struct ParentSampler {
    std::atomic<bool> stop{false},ready{false};std::thread thread;
    POINT first{},last{};HWND firstForeground=nullptr,lastForeground=nullptr;
    ULONGLONG samples=0,cursorChanges=0,foregroundChanges=0,errors=0;DWORD firstError=0;
    ~ParentSampler(){Finish();}
    void Sample()noexcept {
        POINT current{};const BOOL valid=GetCursorPos(&current);
        const DWORD error=valid?ERROR_SUCCESS:GetLastError();
        const HWND foreground=GetForegroundWindow();
        if(!valid||!foreground){++errors;if(!firstError)firstError=error?error:ERROR_INVALID_STATE;}
        if(samples==0){first=current;firstForeground=foreground;}
        else {
            if(valid&&(current.x!=last.x||current.y!=last.y))++cursorChanges;
            if(foreground!=lastForeground)++foregroundChanges;
        }
        if(valid)last=current;lastForeground=foreground;++samples;
    }
    void Start(){thread=std::thread([this]{Sample();ready=true;while(!stop){Sleep(50);if(!stop)Sample();}});}
    void Finish()noexcept{stop=true;if(thread.joinable())thread.join();}
    bool Unchanged()const noexcept{return samples>=2&&errors==0&&cursorChanges==0&&foregroundChanges==0;}
};
struct TaskRun {
    Com<ITaskService> service;Com<ITaskFolder> folder;Com<IRegisteredTask> registered;Com<IRunningTask> running;
    HANDLE receipt=INVALID_HANDLE_VALUE;std::wstring name,path,xml,instance;
    std::string nonce,originalHash;bool created=false,deleted=false,cleanupAttempted=false,identityKnown=false;
    bool runAttempted=false,executionComplete=false,stopAttempted=false,sawRunning=false;
    LONG taskResult=0;HRESULT resultQuery=E_PENDING;
    HRESULT failure=S_OK;const char* failureStage="none";
    ~TaskRun(){Cleanup();}
    bool Record(const char* stage,HRESULT hr=S_OK,const std::string& extra={})noexcept {
        try {
            std::ostringstream out;
            out<<"{\"schema\":\"retsar.child-session-task.v1\",\"stage\":\""<<stage<<"\",\"nonce\":\""<<nonce
               <<"\",\"taskName\":\"RETSAR-ChildProbe-"<<nonce<<"\",\"hresult\":"<<static_cast<ULONG>(hr);
            if(!extra.empty())out<<','<<extra;
            out<<'}';return Append(receipt,out.str()).succeeded;
        }catch(...){return false;}
    }
    bool Check(const char* stage,HRESULT hr)noexcept {
        if(!Record(stage,hr)&&SUCCEEDED(hr))hr=HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        if(FAILED(hr)&&SUCCEEDED(failure)){failure=hr;failureStage=stage;}
        return SUCCEEDED(hr);
    }
    bool Identity() {
        if(!created||!identityKnown||!folder)return false;
        Bstr taskName(name.c_str());Com<IRegisteredTask> current;
        HRESULT hr=folder->GetTask(taskName.value,current.put());
        if(hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)){deleted=true;return false;}
        if(FAILED(hr)||!current)return false;
        Bstr currentXml,currentPath;
        if(FAILED(current->get_Xml(currentXml.put()))||FAILED(current->get_Path(currentPath.put()))||
           !currentXml.value||!currentPath.value)return false;
        std::wstring observed(currentXml.value,SysStringLen(currentXml.value));std::string observedHash;
        if(observed!=xml||std::wstring(currentPath.value,SysStringLen(currentPath.value))!=path||
           FAILED(Hash(reinterpret_cast<const BYTE*>(observed.data()),static_cast<ULONG>(observed.size()*sizeof(wchar_t)),observedHash)))return false;
        return observedHash==originalHash;
    }
    bool InstanceIdentity() {
        if(!running||instance.empty())return false;
        Bstr currentGuid,currentPath;
        return SUCCEEDED(running->get_InstanceGuid(currentGuid.put()))&&SUCCEEDED(running->get_Path(currentPath.put()))&&
            currentGuid.value&&currentPath.value&&
            std::wstring(currentGuid.value,SysStringLen(currentGuid.value))==instance&&
            std::wstring(currentPath.value,SysStringLen(currentPath.value))==path;
    }
    void Cleanup()noexcept {
        if(cleanupAttempted||!created)return;cleanupAttempted=true;
        try {
            if(!Identity()){Record(deleted?"task-already-absent":"cleanup-identity-refused",deleted?S_OK:E_ACCESSDENIED);return;}
            if(runAttempted&&!executionComplete) {
                // An uncertain RunEx cannot be converted into safe deletion by a
                // destructor. Stop only the returned, identity-checked instance.
                if(stopAttempted||!InstanceIdentity()) {
                    Record("cleanup-deferred-instance-uncertain",E_PENDING);return;
                }
                stopAttempted=true;Record("cleanup-stop-own-instance-intent");
                const HRESULT stopped=running->Stop();Record("cleanup-stop-own-instance",stopped);
                const ULONGLONG deadline=GetTickCount64()+5000;
                while(GetTickCount64()<deadline) {
                    TASK_STATE state=TASK_STATE_UNKNOWN;
                    if(SUCCEEDED(registered->get_State(&state))&&state==TASK_STATE_READY&&
                       SUCCEEDED(stopped)){executionComplete=true;break;}
                    Sleep(100);
                }
                if(!executionComplete) {Record("cleanup-deferred-running-task",E_PENDING);return;}
            }
            resultQuery=registered->get_LastTaskResult(&taskResult);
            Record("last-task-result-query",resultQuery);
            Record("last-task-result",S_OK,"\"taskResult\":"+std::to_string(static_cast<ULONG>(taskResult)));
            // Failure to append a cleanup checkpoint must not prevent removal of
            // an identity-verified, stopped task whose creation intent is durable.
            if(!Record("delete-intent")&&SUCCEEDED(failure)) {
                failure=HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);failureStage="delete-intent-write";
            }
            if(!Identity()){Record("delete-identity-refused",E_ACCESSDENIED);return;}
            Bstr taskName(name.c_str());
            const HRESULT hr=folder->DeleteTask(taskName.value,0);deleted=SUCCEEDED(hr);
            Record("delete-result",hr);
            if(FAILED(hr)&&SUCCEEDED(failure)){failure=hr;failureStage="delete-result";}
        }catch(...){Record("cleanup-exception",E_UNEXPECTED);}
    }
};


bool PrepareTask(TaskRun& run,const std::wstring& arguments,const std::wstring& sid) {
    if(!run.Check("scheduler-create",CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITaskService,reinterpret_cast<void**>(run.service.put()))))return false;
    VARIANT empty{};VariantInit(&empty);
    if(!run.Check("scheduler-connect",run.service->Connect(empty,empty,empty,empty)))return false;
    Bstr root(L"\\");
    if(!run.Check("scheduler-folder",run.service->GetFolder(root.value,run.folder.put())))return false;
    Com<ITaskDefinition> definition;
    if(!run.Check("definition-create",run.service->NewTask(0,definition.put())))return false;
    Com<IPrincipal> principal;
    if(!run.Check("principal-get",definition->get_Principal(principal.put())))return false;
    Bstr user(sid.c_str());
    if(!user.value||!run.Check("principal-user",principal->put_UserId(user.value))||
       !run.Check("principal-logon",principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN))||
       !run.Check("principal-runlevel",principal->put_RunLevel(TASK_RUNLEVEL_LUA)))return false;
    Com<ITaskSettings> settings;
    if(!run.Check("settings-get",definition->get_Settings(settings.put())))return false;
    Bstr duration(L"PT35S");
    if(!run.Check("task-enabled",settings->put_Enabled(VARIANT_TRUE))||
       !run.Check("task-demand",settings->put_AllowDemandStart(VARIANT_TRUE))||
       !run.Check("task-battery-start",settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE))||
       !run.Check("task-battery-stop",settings->put_StopIfGoingOnBatteries(VARIANT_FALSE))||
       !run.Check("task-idle",settings->put_RunOnlyIfIdle(VARIANT_FALSE))||
       !run.Check("task-wake",settings->put_WakeToRun(VARIANT_FALSE))||
       !run.Check("task-start-available",settings->put_StartWhenAvailable(VARIANT_FALSE))||
       !run.Check("task-hidden",settings->put_Hidden(VARIANT_TRUE))||
       !run.Check("task-execution-limit",settings->put_ExecutionTimeLimit(duration.value))||
       !run.Check("task-single-instance",settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW)))return false;
    Com<IActionCollection> actions;Com<IAction> action;Com<IExecAction> execution;
    if(!run.Check("actions-get",definition->get_Actions(actions.put()))||
       !run.Check("action-create",actions->Create(TASK_ACTION_EXEC,action.put()))||
       !run.Check("action-exec-interface",action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(execution.put()))))return false;
    Bstr executable(ProbePath),args(arguments.c_str()),working(ProbeDirectory);
    if(!executable.value||!args.value||!working.value||
       !run.Check("action-fixed-path",execution->put_Path(executable.value))||
       !run.Check("action-fixed-arguments",execution->put_Arguments(args.value))||
       !run.Check("action-working-directory",execution->put_WorkingDirectory(working.value)))return false;
    Bstr requestedXml;
    if(!run.Check("definition-xml",definition->get_XmlText(requestedXml.put()))||!requestedXml.value)return false;
    std::string requestedHash;
    if(!run.Check("definition-hash",Hash(reinterpret_cast<const BYTE*>(requestedXml.value),
        SysStringByteLen(requestedXml.value),requestedHash)))return false;
    const std::wstring security=L"D:P(A;;GA;;;SY)(A;;GA;;;"+sid+L")";
    Variant identity,sddl;
    identity.value.vt=VT_BSTR;identity.value.bstrVal=SysAllocString(sid.c_str());
    sddl.value.vt=VT_BSTR;sddl.value.bstrVal=SysAllocString(security.c_str());
    if(!identity.value.bstrVal||!sddl.value.bstrVal)return false;
    Bstr name(run.name.c_str());
    if(!run.Record("register-intent",S_OK,"\"requestedXmlSha256\":\""+requestedHash+"\""))return false;
    const HRESULT registration=run.folder->RegisterTaskDefinition(name.value,definition.value,
        TASK_CREATE|TASK_IGNORE_REGISTRATION_TRIGGERS,identity.value,empty,TASK_LOGON_INTERACTIVE_TOKEN,
        sddl.value,run.registered.put());
    run.created=SUCCEEDED(registration);
    if(FAILED(registration))return run.Check("register-result",registration);
    Bstr actualXml,actualPath;
    if(!run.Check("registered-xml",run.registered->get_Xml(actualXml.put()))||
       !run.Check("registered-path",run.registered->get_Path(actualPath.put()))||!actualXml.value||!actualPath.value)return false;
    run.xml.assign(actualXml.value,SysStringLen(actualXml.value));
    if(std::wstring(actualPath.value,SysStringLen(actualPath.value))!=run.path)return run.Check("registered-path-mismatch",E_UNEXPECTED);
    if(!run.Check("registered-hash",Hash(reinterpret_cast<const BYTE*>(run.xml.data()),
        static_cast<ULONG>(run.xml.size()*sizeof(wchar_t)),run.originalHash)))return false;
    run.identityKnown=true;
    return run.Check("register-result",registration)&&
        run.Record("registered-identity",S_OK,"\"originalXmlSha256\":\""+run.originalHash+"\"");
}
bool EvidenceHash(const std::wstring& path,std::string& hash) {
    Handle file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file.value==INVALID_HANDLE_VALUE||!ExactDiskHandle(file.value,path))return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.value,&size)||size.QuadPart<=0||size.QuadPart>1024*1024)return false;
    std::vector<BYTE> data(static_cast<std::size_t>(size.QuadPart));DWORD read=0;
    if(!ReadFile(file.value,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)||read!=data.size())return false;
    return SUCCEEDED(Hash(data.data(),read,hash));
}
}

int wmain(int argc,wchar_t* argv[]) {
    if(argc==1||(argc==2&&std::wcscmp(argv[1],L"--help")==0)) {
        std::cout<<"RETSAR owned child-session input test launcher\n"
            <<"Usage: RETSAR-child-session-task-probe.exe --launch --expected-child-session <id> --receipt <new.jsonl>\n"
            <<"The explicit mode runs only RETSAR-child-session-input-probe.exe in the current verified child.\n"
            <<"A new .worker.jsonl receipt is derived next to the requested private parent receipt.\n"
            <<"Task: random own name, interactive user, LUA, no triggers, 35 second execution limit.\n"
            <<"Launcher waits up to 45 seconds; only its own task instance may be stopped on failure.\n"
            <<"Task XML and instance identity are checked before stop/delete. No passwords or arbitrary programs.\n"
            <<"The parent cursor and foreground window are sampled, never changed by this executable.\n"
            <<"Input isolation is not certified by a scheduler result; correlate the worker final receipt.\n"
            <<"Help and rejected arguments perform no COM, WTS, task or input operations.\n";return 0;
    }
    DWORD child=0;
    if(argc!=6||std::wcscmp(argv[1],L"--launch")||std::wcscmp(argv[2],L"--expected-child-session")||
       !SessionNumber(argv[3],child)||std::wcscmp(argv[4],L"--receipt")) {
        std::cerr<<"Invalid arguments; no task or session action.\n";return 64;
    }
    try {
        std::wstring parentPath;
        if(!ValidReceiptPath(argv[5],parentPath)){std::cerr<<"Receipt path rejected.\n";return 64;}
        const auto workerPath=parentPath.substr(0,parentPath.size()-6)+L".worker.jsonl";
        std::wstring checkedWorker;
        if(!ValidReceiptPath(workerPath.c_str(),checkedWorker)||checkedWorker!=workerPath)return 64;
        const DWORD workerAttributes=GetFileAttributesW(workerPath.c_str());
        const DWORD workerLookupError=GetLastError();
        if(workerAttributes!=INVALID_FILE_ATTRIBUTES||
           (workerLookupError!=ERROR_FILE_NOT_FOUND&&workerLookupError!=ERROR_PATH_NOT_FOUND))return 3;
        Snapshot baseline{};
        if(!BoundSessions(child,0,baseline)){std::cout<<ToJson(baseline)<<'\n';return 3;}
        const DWORD parent=baseline.caller.value;
        Handle mutex;mutex.value=CreateMutexW(nullptr,TRUE,L"Local\\RETSAR.ChildSessionInputProbe");
        const DWORD mutexError=GetLastError();
        if(!mutex.value||mutexError==ERROR_ALREADY_EXISTS){if(!mutex.value)mutex.value=INVALID_HANDLE_VALUE;return 3;}
        Handle executable;std::string executableHash;
        if(!FixedProbe(executable.value,executableHash)){std::cerr<<"Fixed worker validation failed.\n";return 4;}
        Handle receipt;receipt.value=CreateFileW(parentPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(receipt.value==INVALID_HANDLE_VALUE||!ExactDiskHandle(receipt.value,parentPath))return 4;
        std::array<BYTE,16> random{};
        if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 4;
        constexpr char hex[]="0123456789abcdef";std::string nonce;
        for(BYTE b:random){nonce+=hex[b>>4];nonce+=hex[b&15];}
        const std::wstring wideNonce(nonce.begin(),nonce.end());
        const std::wstring arguments=L"--run --child-session "+std::to_wstring(child)+
            L" --parent-session "+std::to_wstring(parent)+L" --nonce "+wideNonce+
            L" --receipt "+QuoteArgument(workerPath);
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(initialized))return 5;
        struct ComLifetime{~ComLifetime(){CoUninitialize();}} comLifetime;
        TaskRun run;run.receipt=receipt.value;run.nonce=nonce;run.name=L"RETSAR-ChildProbe-"+wideNonce;run.path=L"\\"+run.name;
        std::ostringstream initial;
        initial<<"\"parentSessionId\":"<<parent<<",\"childSessionId\":"<<child
            <<",\"ownerProcessId\":"<<GetCurrentProcessId()<<",\"baseline\":"<<ToJson(baseline)
            <<",\"fixedProbeSha256\":\""<<executableHash<<"\",\"inputIsolationVerified\":false";
        if(!run.Record("baseline",S_OK,initial.str()))return 4;
        std::wstring sid;
        if(!CurrentSid(sid)){run.Check("current-user-sid",HRESULT_FROM_WIN32(GetLastError()));return 4;}
        if(!PrepareTask(run,arguments,sid))return 5;
        Snapshot before{};
        if(!BoundSessions(child,parent,before)||!run.Identity()){run.Check("pre-run-identity",E_ACCESSDENIED);return 5;}
        ParentSampler sampler;sampler.Start();
        const auto samplerStart=GetTickCount64();
        while(!sampler.ready&&GetTickCount64()-samplerStart<1000)Sleep(10);
        if(!sampler.ready){run.Check("parent-sampler-start",HRESULT_FROM_WIN32(ERROR_TIMEOUT));return 5;}
        if(!run.Record("run-intent"))return 5;
        VARIANT empty{};VariantInit(&empty);
        // Documented RunEx session selection; no user/password is supplied.
        // https://learn.microsoft.com/windows/win32/api/taskschd/nf-taskschd-iregisteredtask-runex
        run.runAttempted=true;
        if(!run.Check("RunEx",run.registered->RunEx(empty,TASK_RUN_USE_SESSION_ID,
            static_cast<LONG>(child),nullptr,run.running.put()))||!run.running)return 5;
        Bstr instance;
        if(!run.Check("instance-guid",run.running->get_InstanceGuid(instance.put()))||!instance.value)return 5;
        run.instance.assign(instance.value,SysStringLen(instance.value));
        if(!run.InstanceIdentity()){run.Check("instance-identity",E_ACCESSDENIED);return 5;}
        bool complete=false,timedOut=false,sessionLost=false;TASK_STATE state=TASK_STATE_UNKNOWN;
        const ULONGLONG started=GetTickCount64();
        while(GetTickCount64()-started<DeadlineMs) {
            TASK_STATE instanceState=TASK_STATE_UNKNOWN;
            const HRESULT refreshed=run.running->Refresh();
            if(SUCCEEDED(refreshed)&&SUCCEEDED(run.running->get_State(&instanceState))&&instanceState==TASK_STATE_RUNNING)
                run.sawRunning=true;
            const HRESULT hr=run.registered->get_State(&state);
            if(FAILED(hr)){run.Check("task-state",hr);break;}
            DATE lastRun=0;LONG currentResult=SCHED_S_TASK_HAS_NOT_RUN;
            // READY can be a pre-run snapshot. Require this returned instance to
            // have been observed running, plus a subsequent terminal task result.
            if(run.sawRunning&&state==TASK_STATE_READY&&
               SUCCEEDED(run.registered->get_LastRunTime(&lastRun))&&lastRun>0&&
               SUCCEEDED(run.registered->get_LastTaskResult(&currentResult))&&
               currentResult!=SCHED_S_TASK_HAS_NOT_RUN&&currentResult!=SCHED_S_TASK_RUNNING) {
                complete=true;break;
            }
            Snapshot current{};
            if(!BoundSessions(child,parent,current)){sessionLost=true;run.Check("session-guard-lost",E_ACCESSDENIED);break;}
            Sleep(100);
        }
        if(!complete&&!sessionLost&&GetTickCount64()-started>=DeadlineMs) {
            timedOut=true;run.Check("task-deadline",HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        }
        run.executionComplete=complete;
        run.Cleanup(); // Own-instance stop, if needed, precedes identity-checked deletion.
        complete=run.executionComplete;
        sampler.Finish();
        const LONG taskResult=run.taskResult;const HRESULT resultQuery=run.resultQuery;
        std::string workerHash;const bool evidence=complete&&EvidenceHash(workerPath,workerHash);
        std::ostringstream final;
        final<<"\"parentSessionId\":"<<parent<<",\"childSessionId\":"<<child
            <<",\"taskComplete\":"<<(complete?"true":"false")<<",\"instanceObservedRunning\":"<<(run.sawRunning?"true":"false")<<",\"timedOut\":"<<(timedOut?"true":"false")
            <<",\"sessionGuardLost\":"<<(sessionLost?"true":"false")<<",\"taskDeleted\":"<<(run.deleted?"true":"false")
            <<",\"taskResult\":"<<static_cast<ULONG>(taskResult)<<",\"sampleCount\":"<<sampler.samples
            <<",\"cursorChanges\":"<<sampler.cursorChanges<<",\"foregroundChanges\":"<<sampler.foregroundChanges
            <<",\"sampleErrors\":"<<sampler.errors<<",\"parentStartCursorX\":"<<sampler.first.x
            <<",\"parentStartCursorY\":"<<sampler.first.y<<",\"parentEndCursorX\":"<<sampler.last.x
            <<",\"parentEndCursorY\":"<<sampler.last.y<<",\"parentStartForeground\":"
            <<reinterpret_cast<ULONG_PTR>(sampler.firstForeground)<<",\"parentEndForeground\":"
            <<reinterpret_cast<ULONG_PTR>(sampler.lastForeground)<<",\"parentObservation\":\""
            <<(sampler.Unchanged()?"unchanged-during-sampling":"inconclusive")<<"\",\"workerReceiptPresent\":"
            <<(evidence?"true":"false")<<",\"workerReceiptSha256\":\""<<workerHash
            <<"\",\"inputIsolationVerified\":false";
        const bool saved=run.Record("final",run.failure,final.str());
        std::cout<<"Task completed: "<<(complete?"yes":"no")<<"; task removed: "<<(run.deleted?"yes":"no")
            <<"; parent observation: "<<(sampler.Unchanged()?"unchanged during sampling":"inconclusive")
            <<". Correlate the worker final receipt and nonce.\n";
        return complete&&run.deleted&&SUCCEEDED(resultQuery)&&taskResult==0&&evidence&&saved&&SUCCEEDED(run.failure)?0:2;
    }catch(...){std::cerr<<"Owned task experiment failed; review its private receipt before retrying.\n";return 5;}
}

