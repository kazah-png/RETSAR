#include "backend.hpp"
#include "executor.hpp"
#include "version.hpp"
#include "safe_c_path.hpp"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
seats::Json engineReport(){
    const seats::runtime::WindowsBlockedExecutor executor;
    return seats::Json::Object{
        {"schema","retsar.engine-status.v1"},
        {"name","RETSAR"},
        {"displayName","Runtime Environment for Terminal Sharing and Allocation of Resources"},
        {"version",seats::Version},
        {"license","MIT"},
        {"operationalMultiseat",false},
        {"mode","READ_ONLY_STATUS"},
        {"executor","WindowsBlockedExecutor"},
        {"providerEpoch",executor.providerEpoch()},
        {"windowsCapabilities",seats::capabilityJson(executor.capabilities())},
        {"hostInspected",false},
        {"journalOpened",false},
        {"reason","Native session creation, physical display ownership and independent input are not implemented. No real seat can be started."}
    };
}
void writeNewFile(const seats::LockedCParents& target,const std::string& data){
    bool existed=false;
    HANDLE file=target.openFile(GENERIC_WRITE,CREATE_NEW,existed);
    DWORD written=0;const bool ok=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)!=FALSE&&written==data.size();
    CloseHandle(file);
    if(!ok)throw std::runtime_error("Output write failed; the newly created output may be incomplete.");
}
}
int wmain(int argc,wchar_t** argv){
    try{
        std::wstring output;
        bool engineStatus=false;
        for(int i=1;i<argc;++i){
            const std::wstring option=argv[i];
            if(option==L"--help"||option==L"-h"){
                std::cout<<"RETSAR "<<seats::Version<<" (MIT)\nRuntime Environment for Terminal Sharing and Allocation of Resources\nDevelopment engine; native multiseat is NOT operational.\n"
                    <<"Usage: RETSAR.exe [--json | --engine-status] [--output C:\\existing-folder\\new-report.json]\n"
                    <<"Default: UTF-8 JSON to stdout. --output additionally creates a NEW file on C:.\n"
                    <<"--engine-status reports native readiness without querying the host or opening a journal.\n"
                    <<"No start/assign/install commands are implemented. No disk enumeration or input events are read.\n";return 0;
            }
            if(option==L"--json")continue;
            if(option==L"--engine-status"&&!engineStatus){engineStatus=true;continue;}
            if(option==L"--output"&&i+1<argc&&output.empty()){output=argv[++i];continue;}
            throw std::runtime_error("Unknown, repeated or incomplete argument. Use --help.");
        }
        std::unique_ptr<seats::LockedCParents> outputPath;
        if(!output.empty())outputPath=std::make_unique<seats::LockedCParents>(std::filesystem::path(output));
        seats::WindowsInventoryBackend backend;
        const std::string document=(engineStatus?engineReport():backend.inspect()).dump()+"\n";
        if(outputPath)writeNewFile(*outputPath,document);
        std::cout<<document;return 0;
    }catch(const std::exception& error){
        std::cout<<seats::Json(seats::Json::Object{{"schema","retsar.error.v1"},{"error",error.what()},{"operationalMultiseat",false}}).dump()<<'\n';return 1;
    }
}
