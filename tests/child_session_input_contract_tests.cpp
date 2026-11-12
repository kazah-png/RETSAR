#include "../src/lab/child_session_input_contract.hpp"
#include "../src/lab/child_session_input_diagnostics.hpp"
#include <iostream>
#include <sstream>

namespace {
using namespace seats::lab::child::input;
unsigned passed=0,failed=0;
void check(bool result,const char* name){if(result)++passed;else{++failed;std::cerr<<"FAIL "<<name<<'\n';}}
const std::vector<std::wstring> Arguments={L"--run",L"--child-session",L"3",L"--parent-session",L"2",
    L"--nonce",L"0123456789abcdef0123456789abcdef",L"--receipt",L"D:\\RETSAR-Lab\\RETSAR\\private\\synthetic.worker.jsonl"};
bool rejects(const std::vector<std::wstring>& args){try{(void)Parse(args);return false;}catch(const std::invalid_argument&){return true;}}
Identity IdentityForChild(){Identity id;id.processKnown=id.tokenKnown=id.noImpersonation=true;
    id.processSession=id.tokenSession=3;id.consoleSession=2;id.childActive=id.parentActive=true;return id;}
WindowEvidence OwnedWindow(){return {true,true,true,true,true,true};}
}
int main(){
    const auto options=Parse(Arguments);
    check(!Parse({}).run&&!Parse({L"--help"}).run,"help cannot arm input");
    check(options.run&&options.child==3&&options.parent==2,"explicit child and parent accepted");
    check(rejects({L"--help",L"--run"}),"help cannot conceal run arguments");
    auto args=Arguments;args[0]=L"--connect";check(rejects(args),"ordinary connection mode rejected");
    args=Arguments;args.push_back(L"--run");check(rejects(args),"extra or duplicate arguments rejected");
    args=Arguments;args[3]=L"--child-session";check(rejects(args),"duplicate session option rejected");
    args=Arguments;args.pop_back();check(rejects(args),"missing receipt rejected");
    args=Arguments;args[8]=L"";check(rejects(args),"empty receipt rejected");
    args=Arguments;args[4]=L"3";check(rejects(args),"parent cannot equal child");
    for(const auto* invalid:{L"0",L"4294967295",L"4294967296",L"999999999999999999999999",L"-1",L"+3",L"3x",L""}) {
        args=Arguments;args[2]=invalid;check(rejects(args),"invalid child ID rejected");
        args=Arguments;args[4]=invalid;check(rejects(args),"invalid parent ID rejected");
    }
    for(const auto* invalid:{L"",L"0123456789abcdef",L"0123456789abcdef0123456789abcdeff",L"0123456789abcdef0123456789abcdeF",L"0123456789abcdef0123456789abcdeg"}) {
        args=Arguments;args[6]=invalid;check(rejects(args),"malformed or noncanonical nonce rejected");
    }
    const auto identity=IdentityForChild();const auto window=OwnedWindow();
    check(InputAllowed(options,identity,window),"only exact child identity and owned active window admitted");
    check(!InputAllowed(Parse({}),identity,window),"help never passes input gate");
    for(const DWORD processSession:{0ul,2ul,4ul,static_cast<DWORD>(MAXDWORD)}) {
        auto wrong=identity;wrong.processSession=processSession;
        check(!InputAllowed(options,wrong,window),"session zero, parent, unrelated and sentinel caller rejected");
    }
    for(const DWORD tokenSession:{0ul,2ul,4ul,static_cast<DWORD>(MAXDWORD)}) {
        auto wrong=identity;wrong.tokenSession=tokenSession;
        check(!InputAllowed(options,wrong,window),"token outside exact child rejected");
    }
    for(const DWORD console:{0ul,3ul,4ul,static_cast<DWORD>(MAXDWORD)}) {
        auto wrong=identity;wrong.consoleSession=console;
        check(!InputAllowed(options,wrong,window),"console must remain the expected parent");
    }
    for(const auto member:{&Identity::processKnown,&Identity::tokenKnown,&Identity::noImpersonation,
                          &Identity::childActive,&Identity::parentActive}) {
        auto wrong=identity;wrong.*member=false;
        check(!InputAllowed(options,wrong,window),"unknown identity, impersonation or inactive session rejected");
    }
    for(const auto member:{&WindowEvidence::inputDesktopMatches,&WindowEvidence::foregroundIsOwnedWindow,
                          &WindowEvidence::windowProcessMatches,&WindowEvidence::windowThreadMatches,
                          &WindowEvidence::windowEnabled,&WindowEvidence::windowVisible}) {
        auto wrong=window;wrong.*member=false;
        check(!InputAllowed(options,identity,wrong),"desktop, foreign ownership, focus or window mismatch rejected");
    }
    {
        const RECT client{0,0,504,261};POINT target{};
        const POINT middle{252,130};
        check(diagnostics::ChooseTarget(client,middle,middle,target)&&
              diagnostics::Separated(target,middle),"target moves at least sixteen pixels when cursor starts at client center");
        const POINT previousTarget=target;
        check(diagnostics::ChooseTarget(client,middle,previousTarget,target)&&
              diagnostics::Separated(target,middle)&&diagnostics::Separated(target,previousTarget),
              "target differs from both current and original cursor");
        check(target.x>=16&&target.x<client.right-16&&target.y>=16&&target.y<client.bottom-16,
              "target stays strictly inside the own client rectangle");
        check(!diagnostics::ChooseTarget({0,0,40,40},middle,middle,target),"small window refuses ambiguous target");
        check(!diagnostics::ChooseTarget({20,20,10,10},middle,middle,target),"inverted client rectangle rejected");
        check(!diagnostics::ChooseTarget({0,0,40000,40000},middle,middle,target),"unbounded geometry rejected");
        check(diagnostics::ChooseTarget({-500,-200,500,200},{0,0},{100,0},target)&&
              diagnostics::Separated(target,{0,0})&&diagnostics::Separated(target,{100,0}),
              "negative coordinates preserve separation without overflow");
    }
    {
        diagnostics::MouseMessages messages;
        const auto marker=static_cast<ULONG_PTR>(0x12345678abcdef01ull);
        messages.Observe(marker,marker,false,1,0,0);
        check(messages.total==1&&messages.afterSend==0&&messages.fullMatches==0,
              "pre-send mouse messages remain diagnostic and never match injected input");
        messages.Observe(marker,marker,true,2,12,34);
        messages.Observe(static_cast<ULONG_PTR>(0xabcdef01ull),marker,true,3,12,34);
        messages.Observe(static_cast<ULONG_PTR>(0xffffffffabcdef01ull),marker,true,4,12,34);
        messages.Observe(0,marker,true,5,12,34);
        check(messages.total==5&&messages.afterSend==4&&messages.fullMatches==1,
              "full-width matching remains distinct from total observed messages");
        check(messages.low32OnlyMatches==2&&messages.zeroAfterSend==1,
              "truncated or sign-extended metadata and zero are diagnostic categories only");
        diagnostics::MouseMessages bounded;
        for(ULONG_PTR index=100;index<112;++index)bounded.Observe(index,marker,false,index,-7,-11);
        check(bounded.total==12&&bounded.size==8&&bounded.last[0].extra==104&&bounded.last[7].extra==111,
              "only the last eight own-window message metadata samples are retained");
        std::ostringstream json;bounded.JsonMembers(json);
        check(json.str().find("\"extraInfo\":104")!=std::string::npos&&
              json.str().find("\"extraInfo\":100")==std::string::npos&&
              json.str().find("\"clientX\":-7")!=std::string::npos,
              "bounded diagnostic JSON preserves numeric samples and signed client coordinates");
    }
    {
        // Regression fixture: the owned child transport delivered 782830719
        // for a former 64-bit tag of 11823198694904761471. Generate the narrow
        // tag up front; never mask or reinterpret a received message to match.
        const std::wstring observedNonce=L"9813a2baf92f7168e430589b4af5b1c4";
        const auto marker=CorrelationMarkerFromNonce(observedNonce);
        check(marker==782830719,"observed transport pattern matches the newly generated positive marker exactly");
        check(marker==CorrelationMarkerFromNonce(observedNonce),"marker generation is deterministic for one nonce");
        check(marker!=0&&(marker&~static_cast<ULONG_PTR>(0x7ffffffful))==0,
              "marker is nonzero with every bit above bit thirty cleared");
        check(CorrelationMarkerFromNonce(L"")==0&&CorrelationMarkerFromNonce(L"invalid")==0,
              "unvalidated nonce cannot produce a usable marker");
        bool allPositive=true;
        for(wchar_t digit:L"0123456789abcdef") {
            if(!digit)continue;
            const auto generated=CorrelationMarkerFromNonce(std::wstring(32,digit));
            allPositive=allPositive&&generated>0&&generated<=0x7ffffffful;
        }
        check(allPositive,"canonical low-entropy fixture nonces still produce bounded positive tags");
        diagnostics::MouseMessages strict;
        strict.Observe(11823198694904761471ull,marker,true,1,0,0);
        check(strict.fullMatches==0&&strict.low32OnlyMatches==1,
              "foreign full-width value sharing low bits is not accepted as an exact marker");
        strict.Observe(static_cast<ULONG_PTR>(static_cast<LONG_PTR>(-1)),marker,true,2,0,0);
        strict.Observe(static_cast<ULONG_PTR>(0xffffffff00000000ull)|marker,marker,true,3,0,0);
        check(strict.fullMatches==0,"negative or sign-extended metadata is never coerced into a match");
        strict.Observe(static_cast<DWORD>(0xaabbccdd00000000ull|(marker^1)),marker,true,4,0,0);
        check(strict.fullMatches==0,"truncation of unrelated metadata does not admit the event");
        strict.Observe(marker,marker,true,5,0,0);
        check(strict.fullMatches==1,"only the complete received value equal to the generated tag matches");
    }
    std::cout<<"Child session input contract: "<<passed<<" passed, "<<failed<<" failed; pure values only, no Windows queries or input.\n";
    return failed?1:0;
}
