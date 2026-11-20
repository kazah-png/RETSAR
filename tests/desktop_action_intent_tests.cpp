#include "../src/desktop/desktop_action_intent.hpp"
#include <iostream>
using namespace retsar::desktop;
namespace {
unsigned passed=0,failed=0;
void Check(bool value,const char* name){if(value)++passed;else{++failed;std::cerr<<"FAIL "<<name<<'\n';}}
Facts Baseline(){
    Facts facts;auto& state=facts.session;state.apiAvailable=true;
    state.enabled={true,true,0,false};state.caller={true,true,0,2};
    state.child={true,false,ERROR_NOT_FOUND,MAXDWORD};state.console={true,2};
    facts.rows={{0,WTSDisconnected,false},{2,WTSActive,true}};facts.inventoryComplete=true;
    facts.viewerFilePresent=facts.receiptDirectoryPresent=true;return facts;
}
Facts Resume(bool enabled=true,DWORD child=3){
    auto facts=Baseline();facts.session.enabled.value=enabled;facts.session.child={true,true,0,child};
    facts.rows.push_back({child,WTSDisconnected,true});return facts;
}
intent::ViewerKey Viewer(){return {0x1234,42,123456,L"D:\\RETSAR\\lab\\viewer.exe"};}
intent::Target Show(){auto facts=Resume();facts.verifiedViewerPresent=true;return intent::Capture(facts,Viewer());}
void CaptureTests(){
    const auto create=intent::Capture(Baseline()),resume=intent::Capture(Resume(false)),enabled=intent::Capture(Resume());
    Check(create.valid&&create.decision.action==Action::CreateDesktop,"known new-desktop action can be remembered");
    Check(resume.valid&&resume.decision.action==Action::ResumeDesktop&&resume.decision.child==3,"exact disabled resume target captured");
    Check(enabled.valid&&enabled.decision.action==Action::ResumeEnabledDesktop,"enabled-baseline policy retained in action identity");
    Check(!intent::Capture(Baseline(),{},true).valid&&!intent::Capture(Resume(),{},true).valid,
        "retained launch ownership blocks new create and resume intents");
    auto facts=Resume();facts.verifiedViewerPresent=true;
    Check(intent::Capture(facts,Viewer(),true).valid,"existing verified viewer can still be shown during launch observation");
    Check(!intent::Capture(facts).valid,"show requires full viewer identity");
    for(unsigned missing=0;missing<4;++missing){
        auto viewer=Viewer();if(missing==0)viewer.window=0;else if(missing==1)viewer.process=0;
        else if(missing==2)viewer.created=0;else viewer.path.clear();
        Check(!intent::Capture(facts,viewer).valid,"partial viewer identity cannot authorize focus");
    }
    for(unsigned missing=0;missing<11;++missing){
        facts=Resume();
        switch(missing){
        case 0:facts.inventoryComplete=false;break;
        case 1:facts.rows.clear();break;
        case 2:facts.session.caller.attempted=false;break;
        case 3:facts.session.caller.succeeded=false;break;
        case 4:facts.session.caller.value=0;break;
        case 5:facts.session.console.attempted=false;break;
        case 6:facts.session.console.value=7;break;
        case 7:facts.rows[1].state=WTSDisconnected;break;
        case 8:facts.rows[1].hasUser=false;break;
        case 9:facts.rows.push_back(facts.rows[1]);break;
        case 10:facts.session.child.succeeded=false;facts.session.child.error=ERROR_ACCESS_DENIED;break;
        }
        Check(!intent::Capture(facts).valid,"uncertain session or inventory cannot produce an action target");
    }
    facts=Resume();facts.designPreview=true;Check(!intent::Capture(facts).valid,"design preview never creates an intent target");
    facts=Resume();facts.refreshing=true;Check(!intent::Capture(facts).valid,"actual loading facts never count as fresh admission");
    facts=Resume();facts.launchPending=true;Check(!intent::Capture(facts).valid,"launch-pending facts cannot be bypassed");
    facts=Resume();facts.rows.back().state=WTSActive;Check(!intent::Capture(facts).valid,"active child is never silently resumed");
    facts=Resume();facts.rows.push_back({8,WTSActive,true});Check(!intent::Capture(facts).valid,"another user blocks remembered resume");
    facts=Resume();facts.verifiedViewerPresent=true;facts.session.child.succeeded=false;
    Check(intent::Capture(facts,Viewer()).valid,"showing exact own viewer does not require a mutable child connection decision");
    auto changed=Show();auto identical=changed;identical.viewer.path=L"d:\\retsar\\LAB\\VIEWER.EXE";
    Check(intent::Same(changed,identical),"canonical Windows path casing does not change viewer identity");
    changed=enabled;changed.valid=true;changed.decision.action=Action::None;
    Check(!intent::ValidTarget(changed),"hand-built none action is not an actionable target");
}
void TicketTests(){
    const auto target=intent::Capture(Resume());intent::Ticket ticket;
    Check(ticket.Resolve(target,1,100,101,true,false,false)==intent::Outcome::None,
        "activation or inventory alone cannot create an action");
    Check(ticket.Poll(100,true,false,false)==intent::Outcome::None,"idle timer cannot create an action");
    Check(!ticket.Begin({},100,7)&&!ticket.Begin(target,100,0),"missing target or generation cannot arm intent");
    Check(ticket.Begin(target,100,7)&&ticket.Pending(),"only explicit begin records one requested action");
    Check(!ticket.Begin(intent::Capture(Resume(false)),101,8)&&ticket.Requested().decision.action==Action::ResumeEnabledDesktop,
        "second click cannot replace or widen the pending action");
    Check(ticket.Resolve(target,6,90,150,true,false,false)==intent::Outcome::Waiting&&ticket.Pending(),
        "activation refresh begun before click is never used to execute");
    Check(ticket.Resolve(target,6,110,160,true,false,false)==intent::Outcome::Waiting,
        "new timestamp cannot replace required post-click generation");
    Check(ticket.Resolve(target,7,99,170,true,false,false)==intent::Outcome::Waiting,
        "new generation cannot replace required post-click start time");
    Check(ticket.Resolve(target,7,100,180,true,false,false)==intent::Outcome::Ready&&!ticket.Pending(),
        "identical newly observed action consumes ticket before execution");
    Check(ticket.Resolve(target,8,190,200,true,false,false)==intent::Outcome::None,
        "reentrant completion or duplicate receipt cannot execute twice");
    Check(ticket.Poll(300,true,false,false)==intent::Outcome::None,"completed ticket cannot trigger timer retry");
    ticket.Begin(target,1000,9);ticket.Cancel(intent::Reason::Explicit);
    Check(ticket.Resolve(target,9,1001,1002,true,false,false)==intent::Outcome::None,
        "navigation or manual cancellation cannot be revived by read completion");
    Check(ticket.Begin(target,1100,10),"a new explicit click can start after cancellation");
    Check(ticket.Resolve(target,10,1101,1102,true,false,false)==intent::Outcome::Ready,"new click independently requires new observation");
    ticket.Begin(target,2000,11);
    Check(ticket.Resolve({},10,1900,2001,true,false,false)==intent::Outcome::Cancelled&&ticket.Why()==intent::Reason::Uncertain,
        "error from in-progress observation cancels even before qualifying generation");
    Check(ticket.Resolve(target,11,2002,2003,true,false,false)==intent::Outcome::None,
        "recovered state after error never retries automatically");
}
void ChangedTargets(){
    const auto original=intent::Capture(Resume());
    for(unsigned kind=0;kind<5;++kind){
        intent::Ticket ticket;ticket.Begin(original,100,2);intent::Target changed;
        if(kind==0)changed=intent::Capture(Resume(false));
        else if(kind==1)changed=intent::Capture(Baseline());
        else if(kind==2)changed=intent::Capture(Resume(true,4));
        else if(kind==3){auto facts=Resume();facts.session.caller.value=facts.session.console.value=5;facts.rows[1].id=5;changed=intent::Capture(facts);}
        else changed=Show();
        Check(changed.valid&&ticket.Resolve(changed,2,101,102,true,false,false)==intent::Outcome::Cancelled&&
            ticket.Why()==intent::Reason::Changed,"mode child caller create or show changes never retarget a click");
    }
    const auto viewerTarget=Show();
    for(unsigned field=0;field<4;++field){
        intent::Ticket ticket;ticket.Begin(viewerTarget,100,2);auto changed=viewerTarget;
        if(field==0)++changed.viewer.window;else if(field==1)++changed.viewer.process;
        else if(field==2)++changed.viewer.created;else changed.viewer.path=L"D:\\OtherPreview\\viewer.exe";
        Check(ticket.Resolve(changed,2,101,102,true,false,false)==intent::Outcome::Cancelled,
            "HWND PID creation or preview path change cancels showing a viewer");
    }
    intent::Ticket same;Check(same.Begin(viewerTarget,100,2),"verified show action can be queued");
    Check(same.Resolve(viewerTarget,2,101,102,true,false,false)==intent::Outcome::Ready,"same exact viewer can be shown once after revalidation");
}
void LifecycleTests(){
    const auto target=intent::Capture(Resume());
    for(unsigned condition=0;condition<3;++condition){
        intent::Ticket ticket;ticket.Begin(target,100,2);
        const bool active=condition!=0,closing=condition==1,busy=condition==2;
        Check(ticket.Poll(101,active,closing,busy)==intent::Outcome::Cancelled&&!ticket.Pending(),
            "deactivation closing or launch transition cancels pending intent");
        Check(ticket.Resolve(target,2,102,103,true,false,false)==intent::Outcome::None,
            "returning to a safe state never revives a cancelled click");
    }
    intent::Ticket before;before.Begin(target,100,2);
    Check(before.Resolve(target,2,101,100+intent::DeadlineMs-1,true,false,false)==intent::Outcome::Ready,
        "fresh action immediately before deadline may run");
    intent::Ticket expired;expired.Begin(target,100,2);
    Check(expired.Resolve(target,2,101,100+intent::DeadlineMs,true,false,false)==intent::Outcome::Cancelled&&
        expired.Why()==intent::Reason::Timeout,"deadline applies even if UI timer was delayed");
    intent::Ticket timer;timer.Begin(target,100,2);
    Check(timer.Poll(100+intent::DeadlineMs,true,false,false)==intent::Outcome::Cancelled&&timer.Why()==intent::Reason::Timeout,
        "timer expires a request even when inventory never finishes");
    intent::Ticket clock;clock.Begin(target,100,2);
    Check(clock.Poll(99,true,false,false)==intent::Outcome::Cancelled&&clock.Why()==intent::Reason::ClockChanged,
        "clock reversal cannot extend a pending request");
    intent::Ticket future;future.Begin(target,100,2);
    Check(future.Resolve(target,2,103,102,true,false,false)==intent::Outcome::Cancelled&&future.Why()==intent::Reason::ClockChanged,
        "future-dated observation cannot authorize an action");
}
}
int main(){
    CaptureTests();TicketTests();ChangedTargets();LifecycleTests();
    std::cout<<"Desktop action intent: "<<passed<<" passed, "<<failed
        <<" failed; supplied observations only, no windows, focus, processes or sessions.\n";
    return failed?1:0;
}