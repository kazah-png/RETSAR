#include "../src/lab/probe_args.hpp"
#include <iostream>

namespace {
unsigned passed=0,failed=0;
void check(bool condition,const char* description){
    (condition?++passed:++failed);
    std::cout<<(condition?"PASS PROBE ":"FAIL PROBE ")<<description<<'\n';
}
bool rejects(const std::vector<std::wstring>& args){
    try{(void)seats::lab::parseProbeArgs(args);return false;}
    catch(const std::invalid_argument&){return true;}
}
}
int main(){
    using namespace seats::lab;
    check(!parseProbeArgs({}).interactive,"default is help without host observation");
    check(!parseProbeArgs({L"--help"}).interactive,"explicit help is inert");
    const auto valid=parseProbeArgs({L"--interactive",L"--expected-session",L"7",L"--reference-session",L"2"});
    check(valid.interactive&&valid.expectedSession==7&&valid.referenceSession==2&&!valid.output,"explicit session pair accepted");
    check(currentSessionAllowed(valid,7),"session gate accepts exact candidate");
    check(!currentSessionAllowed(valid,2),"session gate refuses reference session");
    check(!currentSessionAllowed(valid,8),"session gate refuses unrelated session");
    check(!currentSessionAllowed(parseProbeArgs({}),0),"help options never pass session gate");
    check(rejects({L"--interactive"}),"missing session IDs refused");
    check(rejects({L"--expected-session",L"7",L"--reference-session",L"2"}),"session IDs cannot activate interactive mode");
    check(rejects({L"--interactive",L"--expected-session",L"7"}),"missing reference refused");
    check(rejects({L"--interactive",L"--expected-session",L"7",L"--reference-session",L"7"}),"same session pair refused");
    check(rejects({L"--interactive",L"--expected-session",L"0",L"--reference-session",L"2"}),"session zero refused");
    check(rejects({L"--interactive",L"--expected-session",L"4294967295",L"--reference-session",L"2"}),"special session sentinel refused");
    check(rejects({L"--interactive",L"--expected-session",L"4294967296",L"--reference-session",L"2"}),"32-bit overflow refused");
    check(rejects({L"--interactive",L"--expected-session",L"999999999999999999999999",L"--reference-session",L"2"}),"very large number refused without overflow");
    check(rejects({L"--interactive",L"--expected-session",L"-1",L"--reference-session",L"2"}),"negative ID refused");
    check(rejects({L"--interactive",L"--expected-session",L"+7",L"--reference-session",L"2"}),"signed ID refused");
    check(rejects({L"--interactive",L"--expected-session",L"7x",L"--reference-session",L"2"}),"partially numeric ID refused");
    check(rejects({L"--interactive",L"--expected-session",L"7",L"--reference-session"}),"missing value refused");
    check(rejects({L"--interactive",L"--interactive",L"--expected-session",L"7",L"--reference-session",L"2"}),"duplicate mode refused");
    check(rejects({L"--interactive",L"--expected-session",L"7",L"--reference-session",L"2",L"--expected-session",L"8"}),"duplicate candidate refused");
    check(rejects({L"--interactive",L"--expected-session",L"7",L"--reference-session",L"2",L"--reference-session",L"3"}),"duplicate reference refused");
    check(rejects({L"--help",L"--interactive"}),"help cannot hide other arguments");
    check(rejects({L"--unknown"}),"unknown option refused");
    const std::vector<std::wstring> prefix{L"--interactive",L"--expected-session",L"7",L"--reference-session",L"2"};
    auto output=prefix;output.insert(output.end(),{L"--output",L"C:\\probe\\new.json"});
    check(parseProbeArgs(output).output==L"C:\\probe\\new.json","ordinary C output accepted syntactically");
    auto relative=prefix;relative.insert(relative.end(),{L"--output",L"probe.json"});
    check(rejects(relative),"relative output refused before filesystem access");
    auto otherDrive=prefix;otherDrive.insert(otherDrive.end(),{L"--output",L"D:\\probe.json"});
    check(rejects(otherDrive),"other-drive output refused before filesystem access");
    auto directory=prefix;directory.insert(directory.end(),{L"--output",L"C:\\probe\\"});
    check(rejects(directory),"directory-only output refused");
    output.insert(output.end(),{L"--output",L"C:\\probe\\other.json"});
    check(rejects(output),"duplicate output refused");
    std::cout<<"PROBE TOTAL "<<passed<<" passed, "<<failed<<" failed; pure argument/session gate tests, no host observation.\n";
    return failed?1:0;
}
