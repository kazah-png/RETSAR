#include "../src/desktop/desktop_model.hpp"
#include <iostream>
using namespace retsar::desktop;
int main(){
 unsigned passed=0,failed=0;const auto check=[&](bool result,const char* label){std::cout<<(result?"PASS ":"FAIL ")<<label<<'\n';result?++passed:++failed;};
 Facts base;auto& s=base.session;s.apiAvailable=true;s.enabled={true,true,0,false};s.caller={true,true,0,2};s.child={true,false,ERROR_NOT_FOUND,MAXDWORD};s.console={true,2};
 base.inventoryComplete=true;base.rows={{0,WTSDisconnected,false},{2,WTSActive,true}};base.viewerFilePresent=true;base.receiptDirectoryPresent=true;
 check(Decide(base).action==Action::CreateDesktop,"known supported new-session baseline offers an explicit viewer launch");
 auto variant=base;variant.session.child={true,true,0,MAXDWORD};check(Decide(variant).action==Action::CreateDesktop,"successful absent child ID offers new desktop");
 variant=base;variant.session.child.error=ERROR_ACCESS_DENIED;check(Decide(variant).action==Action::None,"access denial is not rewritten as child absence");
 variant=base;variant.session.child.attempted=false;check(Decide(variant).action==Action::None,"unqueried child state blocks launch");
 variant=base;variant.rows.push_back({3,WTSActive,true});check(Decide(variant).action==Action::None,"another signed-in session blocks creation");
 variant=base;variant.rows.push_back({2,WTSActive,true});check(Decide(variant).action==Action::None,"duplicate inventory identities block creation");
 variant=base;variant.rows[1].state=WTSDisconnected;check(Decide(variant).action==Action::None,"inactive parent blocks creation");
 variant=base;variant.session.console.value=7;check(Decide(variant).availability==Availability::UnsupportedContext,"non-console context explains why creation is unavailable");
 variant=base;variant.session.caller.value=0;check(Decide(variant).action==Action::None,"service-session caller cannot launch");
 variant=base;variant.inventoryComplete=false;check(Decide(variant).action==Action::None,"incomplete inventory never enables a launch");
 variant=base;variant.session.enabled.succeeded=false;check(Decide(variant).action==Action::None,"unknown feature state blocks mutation");
 variant=base;variant.session.enabled.value=true;check(Decide(variant).availability==Availability::FeatureInUse&&!CanLaunch(Decide(variant)),"preenabled feature is left alone");
 variant=base;variant.viewerFilePresent=false;check(Decide(variant).availability==Availability::MissingComponents,"missing viewer gets a setup explanation");
 variant=base;variant.receiptDirectoryPresent=false;check(!CanLaunch(Decide(variant)),"missing receipt folder blocks launch");
 Facts resume=base;resume.session.child={true,true,0,3};resume.rows.push_back({3,WTSDisconnected,true});
 auto decision=Decide(resume);check(decision.action==Action::ResumeDesktop&&decision.availability==Availability::ReadyToResume&&decision.child==3,"exact disconnected related child offers resumption");
 variant=resume;variant.rows.back().state=WTSActive;check(!CanLaunch(Decide(variant)),"active child is never passed to resume mode");
 variant=resume;variant.session.child.value=4;check(!CanLaunch(Decide(variant)),"different child relationship blocks resumption");
 variant=resume;variant.rows.back().hasUser=false;check(!CanLaunch(Decide(variant)),"disconnected row without user is not resumable");
 variant=resume;variant.rows.push_back({4,WTSDisconnected,true});check(!CanLaunch(Decide(variant)),"another signed-in child prevents a resume action");
 variant=resume;variant.session.enabled.value=true;variant.rows.back().state=WTSActive;check(Decide(variant).availability==Availability::AlreadyActive&&Decide(variant).action==Action::None,"active child without verified viewer does not trigger duplicate launch");
 variant=resume;variant.verifiedViewerPresent=true;check(Decide(variant).action==Action::ShowViewer&&Decide(variant).availability==Availability::ViewerOpen,"verified existing viewer offers focus without claiming an active desktop");
 variant.refreshing=true;check(Decide(variant).action==Action::None,"refresh suppresses action even with older viewer evidence");
 variant=base;variant.launchPending=true;check(Decide(variant).action==Action::None,"pending launch prevents double submission");
 variant=resume;variant.designPreview=true;variant.verifiedViewerPresent=true;check(Decide(variant).action==Action::None,"design preview cannot start or focus a viewer");
 check(!CanLaunch({Action::ShowViewer,Availability::AlreadyActive}),"showing an existing viewer is not a session launch");
 check(CanLaunch({Action::ResumeDesktop,Availability::Ready,3}),"resume is an explicit launch action");
 // Listener classification must not depend on the numeric session ID.
 for(int state=-1;state<=11;++state)
     check(RequiresUserObservation(state)==(state!=WTSListen),"only the documented listener state skips user observation");
 check(RequiresUserObservation(65536),"future WTS state still requires a user query");
 variant=base;variant.rows.push_back({65536,WTSListen,false});
 check(Decide(variant).action==Action::CreateDesktop,"preserved listener does not block an otherwise complete new-desktop inventory");
 variant=resume;variant.rows.push_back({65536,WTSListen,false});
 check(Decide(variant).action==Action::ResumeDesktop,"preserved listener does not block exact disconnected-child resumption");
 variant=base;variant.rows[1]={2,WTSListen,false};
 check(!CanLaunch(Decide(variant)),"a listener cannot stand in for the active parent");
 variant=resume;variant.rows.back()={3,WTSListen,false};
 check(!CanLaunch(Decide(variant)),"a listener cannot stand in for the resumable child");
 variant=base;variant.rows.push_back({65536,WTSListen,false});variant.rows.push_back({65536,WTSListen,false});
 check(!CanLaunch(Decide(variant)),"duplicate listener identifiers still block launch");
 variant=base;variant.rows.push_back({65536,WTSListen,false});variant.inventoryComplete=false;
 check(!CanLaunch(Decide(variant)),"a listener never masks a failed applicable-session query");
 variant=base;variant.rows.push_back({3,65536,true});
 check(!CanLaunch(Decide(variant)),"an unfamiliar state with a signed-in user still blocks launch");
 // Returning to the console never performs a connection action. Bursts of
 // activation and nested launch messages yield only one fresh observation.
 ActivationRefresh activation;
 check(!activation.Schedule(false,false,false),"idle console schedules no periodic work");
 activation.SetActive(true,false,false);
 check(activation.Schedule(false,false,false),"returning to console queues one read-only observation");
 activation.SetActive(true,false,false);
 check(!activation.Schedule(false,false,false),"duplicate activation cannot queue another message");
 check(activation.Take(false,false,false)&&!activation.Take(false,false,false),"posted observation is consumed exactly once");
 check(!activation.Schedule(false,false,false),"completion does not create an idle refresh loop");
 activation.SetActive(false,false,false);activation.SetActive(true,false,false);
 check(!activation.Schedule(true,false,false),"busy refresh or launch defers the activation request");
 check(activation.Schedule(false,false,false),"request survives the busy interval");
 check(!activation.Take(true,false,false),"a nested busy message cannot begin inventory work");
 check(activation.Schedule(false,false,false)&&activation.Take(false,false,false),"deferred message is consumed after the busy interval ends");
 activation.SetActive(false,false,false);activation.SetActive(true,false,false);
 check(activation.Schedule(false,false,false),"second genuine return schedules new observation");
 activation.SetActive(false,false,false);
 check(!activation.Take(false,false,false)&&!activation.Schedule(false,false,false),"leaving before dispatch avoids background activation work");
 activation.SetActive(true,false,false);
 check(activation.Schedule(false,false,false),"returning again recovers the pending observation");
 activation.PostFailed();
 check(activation.Schedule(false,false,false)&&activation.Take(false,false,false),"failed post retains the request for a later boundary");
 activation.SetActive(false,false,false);activation.SetActive(true,true,false);
 check(!activation.Schedule(false,true,false)&&!activation.Take(false,true,false),"design preview never schedules a Windows inventory");
 activation.SetActive(true,false,false);activation.Schedule(false,false,false);
 activation.SetActive(false,false,true);
 check(!activation.Take(false,false,true)&&!activation.Schedule(false,false,true),"shutdown cancels pending and posted activation work");
 variant=resume;variant.verifiedViewerPresent=true;
 check(Decide(variant).availability==Availability::ViewerOpen&&!CanLaunch(Decide(variant)),"visible viewer blocks resume even with a disconnected child");
 variant=resume;variant.viewerFilePresent=false;
 check(Decide(variant).availability==Availability::MissingComponents&&!CanLaunch(Decide(variant)),"resumption explanation is unavailable until components are ready");
 variant=resume;variant.inventoryComplete=false;
 check(Decide(variant).availability!=Availability::ReadyToResume&&!CanLaunch(Decide(variant)),"incomplete inventory never advertises recoverable desktop");
 // Enabled resumption preserves the global baseline and still requires every
 // existing relation/inventory guard. The GUI selects a distinct explicit mode.
 Facts enabledResume=resume;enabledResume.session.enabled.value=true;
 decision=Decide(enabledResume);
 check(decision.action==Action::ResumeEnabledDesktop&&decision.child==3&&decision.availability==Availability::ReadyToResumeEnabled,"enabled exact child offers explicit baseline-preserving resume");
 check(IsResume(decision)&&CanLaunch(decision)&&std::wcscmp(LaunchMode(decision),L"--resume-enabled-child-session")==0,"enabled resume keeps target and selects its separate viewer mode");
 check(std::wcscmp(LaunchMode(Decide(resume)),L"--resume-child-session")==0&&!IsResume(Decide(base)),"disabled resume and new desktop retain their original modes");
 check(LaunchMode({})==nullptr,"non-launch decision has no executable mode");
 variant=enabledResume;variant.verifiedViewerPresent=true;check(!CanLaunch(Decide(variant)),"existing viewer also blocks baseline-preserving resume");
 variant=enabledResume;variant.inventoryComplete=false;check(!CanLaunch(Decide(variant)),"enabled setting does not mask incomplete inventory");
 variant=enabledResume;variant.rows.push_back({4,WTSDisconnected,true});check(!CanLaunch(Decide(variant)),"enabled resume rejects another signed-in user");
 variant=enabledResume;variant.rows.push_back({3,WTSDisconnected,true});check(!CanLaunch(Decide(variant)),"enabled resume rejects duplicate child IDs");
 variant=enabledResume;variant.session.child.value=4;check(!CanLaunch(Decide(variant)),"enabled resume cannot choose an unrelated child");
 variant=enabledResume;variant.session.child.succeeded=false;check(!CanLaunch(Decide(variant)),"enabled resume requires successful parent-child query");
 variant=enabledResume;variant.session.console.value=4;check(!CanLaunch(Decide(variant)),"enabled resume requires current console parent");
 variant=enabledResume;variant.rows[1].state=WTSDisconnected;check(!CanLaunch(Decide(variant)),"enabled resume rejects disconnected parent");
 variant=enabledResume;variant.rows.back().hasUser=false;check(!CanLaunch(Decide(variant)),"enabled resume rejects unoccupied child");
 variant=enabledResume;variant.viewerFilePresent=false;check(!CanLaunch(Decide(variant)),"enabled resume still needs the local viewer");
 variant=enabledResume;variant.receiptDirectoryPresent=false;check(!CanLaunch(Decide(variant)),"enabled resume still needs a receipt directory");
 variant=enabledResume;variant.session.enabled.succeeded=false;check(!CanLaunch(Decide(variant)),"enabled baseline must be known");
 for(const auto state:{WTSActive,WTSConnected,WTSConnectQuery,WTSShadow,WTSIdle,WTSListen,WTSReset,WTSDown,WTSInit}) {
     variant=enabledResume;variant.rows.back().state=state;
     check(!CanLaunch(Decide(variant)),"enabled target must be exactly disconnected");
 }
 check(BoundedPercent(0)==0&&BoundedPercent(100)==100&&BoundedPercent(MAXDWORD)==100,"out-of-range memory percentage cannot overflow the display");
 std::cout<<"Desktop model: "<<passed<<" passed, "<<failed<<" failed; pure observations only, no Windows query or launch.\n";return failed?1:0;
}