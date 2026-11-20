#include "../src/desktop/launch_observation.hpp"
#include <iostream>
using namespace retsar::desktop;
int main(){
 unsigned passed=0,failed=0;const auto check=[&](bool ok,const char* why){if(ok)++passed;else{++failed;std::cerr<<"FAIL "<<why<<'\n';}};
 Facts f;f.inventoryComplete=true;f.verifiedViewerPresent=true;
 auto& s=f.session;s.apiAvailable=true;s.enabled={true,true,0,true};s.caller={true,true,0,2};s.child={true,true,0,3};s.console={true,2};
 f.rows={{2,WTSActive,true},{3,WTSActive,true}};
 const launch::Evidence good{true,true,true,true,100,2,MAXDWORD};
 check(launch::Evaluate(good,f)==launch::Result::Connected,"fresh exact viewer and active child finish observation");
 auto e=good;e.fresh=false;check(launch::Evaluate(e,f)==launch::Result::Waiting,"timer cannot confirm using stale snapshot");
 e=good;e.exactViewer=false;check(launch::Evaluate(e,f)==launch::Result::Waiting,"another process or reused process identity cannot confirm");
 e=good;e.requestedChild=4;check(launch::Evaluate(e,f)==launch::Result::Waiting,"resume must observe exact requested child");
 e.requestedChild=3;check(launch::Evaluate(e,f)==launch::Result::Connected,"exact resume accepted");
 e=good;e.parent=4;check(launch::Evaluate(e,f)==launch::Result::Waiting,"changed parent cannot confirm");
 e=good;e.elapsedMs=launch::ObservationLimitMs-1;check(launch::Evaluate(e,f)==launch::Result::Connected,"connection before deadline accepted");
 e.elapsedMs=launch::ObservationLimitMs;check(launch::Evaluate(e,f)==launch::Result::TimedOut,"deadline stops even apparently connected stale work");
 e=good;e.processAlive=false;check(launch::Evaluate(e,f)==launch::Result::Exited,"process exit beats old connection evidence");
 e=good;e.processKnown=false;check(launch::Evaluate(e,f)==launch::Result::Unavailable,"wait failure does not imply exit or success");
 auto v=f;v.verifiedViewerPresent=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"session without viewer is not launch confirmation");
 v=f;v.inventoryComplete=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"incomplete inventory never confirms");
 v=f;v.rows[1].state=WTSDisconnected;check(launch::Evaluate(good,v)==launch::Result::Waiting,"disconnected child is not connected");
 v=f;v.rows[1].hasUser=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"empty child cannot confirm");
 v=f;v.rows[0].state=WTSDisconnected;check(launch::Evaluate(good,v)==launch::Result::Waiting,"inactive parent cannot confirm");
 v=f;v.rows.push_back(v.rows[1]);check(launch::Evaluate(good,v)==launch::Result::Waiting,"duplicate child rows ambiguous");
 v=f;v.session.enabled.value=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"disabled connection cannot confirm");
 v=f;v.session.child.succeeded=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"failed relationship query cannot confirm");
 v=f;v.session.child.value=MAXDWORD;check(launch::Evaluate(good,v)==launch::Result::Waiting,"absent child cannot confirm");
 v=f;v.session.console.value=3;check(launch::Evaluate(good,v)==launch::Result::Waiting,"changed console cannot confirm");
 v=f;v.session.enabled.attempted=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"unattempted enabled query cannot confirm despite success flag");
 v=f;v.session.caller.attempted=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"unattempted caller query cannot confirm despite success flag");
 v=f;v.session.child.attempted=false;check(launch::Evaluate(good,v)==launch::Result::Waiting,"unattempted child query cannot confirm despite success flag");
 e=good;e.parent=0;check(launch::Evaluate(e,f)==launch::Result::Waiting,"noninteractive parent cannot confirm");
 std::cout<<"Desktop launch: "<<passed<<" passed, "<<failed<<" failed; pure launch evidence only, no process or session actions.\n";return failed?1:0;
}
