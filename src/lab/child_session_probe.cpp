#include "child_session_status.hpp"
#include <cwchar>
#include <iostream>

// The executable is inert unless the sole argument is --status. It does not
// enable child sessions, instantiate an RDP control, connect, disconnect or log
// off any session. Host queries are deliberately excluded from build checks.
int wmain(int argc, wchar_t* argv[]) {
    if(argc==1||(argc==2&&std::wcscmp(argv[1],L"--help")==0)) {
        std::cout
            <<"RETSAR child-session diagnostic\n"
            <<"Usage: RETSAR-child-session-probe.exe --status\n"
            <<"       RETSAR-child-session-probe.exe --help\n\n"
            <<"--status calls documented read-only Windows APIs and writes JSON.\n"
            <<"Exit 0: queries succeeded; exit 2: one or more queries failed.\n"
            <<"Exit 64: invalid arguments, with no Windows session queries.\n"
            <<"A returned session ID does not prove a usable or independent seat.\n"
            <<"Windows Home support and physical display/input are unverified.\n";
        return 0;
    }
    if(argc!=2||std::wcscmp(argv[1],L"--status")!=0) {
        std::cerr<<"Use --status or --help as the sole argument. No session query was made.\n";
        return 64;
    }
    const auto status=seats::lab::child::Query(seats::lab::child::SystemApi());
    std::cout<<seats::lab::child::ToJson(status)<<'\n';
    return status.apiAvailable&&status.enabled.succeeded&&status.child.succeeded&&
        status.caller.succeeded?0:2;
}
