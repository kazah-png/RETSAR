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
 auto decision=Decide(resume);check(decision.action==Action::ResumeDesktop&&decision.child==3,"exact disconnected related child offers resumption");
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
 check(BoundedPercent(0)==0&&BoundedPercent(100)==100&&BoundedPercent(MAXDWORD)==100,"out-of-range memory percentage cannot overflow the display");
 std::cout<<"Desktop model: "<<passed<<" passed, "<<failed<<" failed; pure observations only, no Windows query or launch.\n";return failed?1:0;
}