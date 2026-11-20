#include "../src/lab/child_session_presentation.hpp"
#include <iostream>
using namespace seats::lab::child::presentation;
namespace {
Catalog Screens(){return {true,{{1,{0,0,1920,1080},{0,0,1920,1040},true},{2,{-1600,-200,0,700},{-1600,-200,0,660},false}}};}
struct FakeWindow final:WindowApi {
    Snapshot current{WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,{100,100,900,700},{0,0,1920,1040}};
    unsigned inspections=0,styles=0,places=0,failInspect=0,failStyle=0,failPlace=0,ignorePlace=0;
    bool alwaysFailPlace=false;
    State* reenter=nullptr;DWORD nestedError=0;
    bool Inspect(Snapshot& out,DWORD& error)noexcept override {++inspections;if(inspections==failInspect){error=ERROR_ACCESS_DENIED;return false;}out=current;error=0;return true;}
    bool Style(LONG_PTR value,DWORD& error)noexcept override {
        ++styles;if(reenter){const auto result=Enter(*reenter,*this,Screens());nestedError=result.error;reenter=nullptr;}
        if(styles==failStyle){error=ERROR_ACCESS_DENIED;return false;}current.style=value;error=0;return true;
    }
    bool Place(const RECT& rect,DWORD& error)noexcept override {
        ++places;if(places==failPlace||alwaysFailPlace){error=ERROR_ACCESS_DENIED;return false;}
        if(places!=ignorePlace)current.rect=rect;error=0;return true;
    }
};
}
int main(){
    unsigned passed=0,failed=0;const auto check=[&](bool value,const char* label){if(value)++passed;else{++failed;std::cerr<<"FAIL "<<label<<'\n';}};
    auto catalog=Screens();auto selected=Secondary(catalog);
    check(selected.found&&selected.monitor.token==2,"exactly one secondary monitor is selected");
    check(Equal(selected.monitor.bounds,{-1600,-200,0,700}),"negative monitor coordinates remain physical pixels");
    check(Valid({LONG_MIN,0,LONG_MIN+100,100}),"negative coordinates near LONG_MIN are valid when dimensions fit");
    check(!Valid({LONG_MIN,0,LONG_MAX,100}),"overflowing monitor width is rejected");
    check(!Valid({0,0,0,100})&&!Valid({10,0,0,100}),"empty and inverted rectangles are rejected");
    auto variant=catalog;variant.complete=false;check(!Secondary(variant).found,"partial enumeration is not a presentation target");
    variant=catalog;variant.monitors.pop_back();check(Secondary(variant).error==ERROR_NOT_FOUND,"primary monitor alone is never selected as secondary");
    variant=catalog;variant.monitors.push_back({3,{1920,0,3200,900},{1920,0,3200,860},false});check(Secondary(variant).error==ERROR_MORE_DATA,"multiple secondary screens require an explicit future selection");
    variant=catalog;variant.monitors[1].token=1;check(Secondary(variant).error==ERROR_DUP_NAME,"duplicate native monitor identities are rejected");
    variant=catalog;variant.monitors[1].bounds=variant.monitors[0].bounds;variant.monitors[1].work=variant.monitors[0].work;check(Secondary(variant).error==ERROR_DUP_NAME,"cloned identical rectangles cannot masquerade as a second screen");
    variant=catalog;variant.monitors[1].primary=true;check(!Secondary(variant).found,"multiple primary reports are invalid");
    variant=catalog;variant.monitors[0].primary=false;check(!Secondary(variant).found,"inventory without a primary is incomplete for this policy");
    variant=catalog;variant.monitors[1].work.left=-1800;check(!Secondary(variant).found,"work area outside monitor bounds is invalid");
    variant=catalog;variant.monitors[1].token=0;check(!Secondary(variant).found,"missing monitor identity is rejected");
    variant=catalog;variant.monitors.resize(65);check(!Secondary(variant).found,"monitor inventory has a bounded size");
    Monitor fresh{};check(!SameTarget(catalog,catalog.monitors[0],fresh),"primary can never be an existing presentation target");
    check(SameTarget(catalog,catalog.monitors[1],fresh),"fresh matching secondary identity remains usable");
    variant=catalog;variant.monitors[1].token=3;check(!SameTarget(variant,catalog.monitors[1],fresh),"replaced secondary cannot silently take over presentation");
    Snapshot original{WS_OVERLAPPEDWINDOW|WS_VISIBLE,{100,100,900,700},{0,0,1920,1040}};RECT restore{};
    check(RestoreRect(original,catalog,restore)&&Equal(restore,original.rect),"unchanged original work area restores exact screen rectangle");
    original.rect={-1500,-100,-700,500};original.work=catalog.monitors[1].work;
    check(RestoreRect(original,catalog,restore)&&Equal(restore,original.rect),"negative original screen coordinates restore without workspace conversion");
    variant=catalog;variant.monitors.pop_back();check(RestoreRect(original,variant,restore)&&Within(restore,variant.monitors[0].work),"unplugged source monitor returns window into remaining work area");
    original.rect={-1900,-500,1900,1800};check(RestoreRect(original,variant,restore)&&Equal(restore,variant.monitors[0].work),"oversized saved window is clamped to available work area");
    variant.monitors[0].bounds={-1920,-1080,0,0};variant.monitors[0].work={-1880,-1080,0,-40};
    check(RestoreRect(original,variant,restore)&&Within(restore,variant.monitors[0].work),"fallback respects negative origin and left/top taskbars");
    check(!RestoreRect(original,Catalog{},restore),"missing fallback geometry is not claimed visible");
    State state;FakeWindow api;const auto saved=api.current;auto result=Enter(state,api,catalog);
    check(result.succeeded&&result.verified&&state.mode==Mode::Presented&&state.saved,"entry commits only after style and rectangle readback");
    check(api.current.style==Borderless(saved.style)&&Equal(api.current.rect,catalog.monitors[1].bounds),"entry changes only owned style and native monitor rectangle");
    check((api.current.style&WS_CLIPCHILDREN)!=0&&(api.current.style&WS_VISIBLE)!=0,"owned clipping and visibility styles are retained");
    check(api.styles==1&&api.places==1&&api.inspections==2,"entry uses bounded native calls with one readback");
    const auto duplicate=Enter(state,api,catalog);check(!duplicate.attempted&&duplicate.error==ERROR_BUSY&&api.styles==1,"repeated entry cannot overwrite the saved window");
    result=Exit(state,api,catalog);check(result.succeeded&&state.mode==Mode::Windowed&&!state.saved,"successful exit clears presentation ownership");
    check(api.current.style==saved.style&&Equal(api.current.rect,saved.rect),"exit restores exact style and position when original screen remains");
    check(!Exit(state,api,catalog).attempted,"repeated exit has no native mutation");
    state={};api=FakeWindow{};api.current.style|=WS_MAXIMIZE;result=Enter(state,api,catalog);
    check(!result.attempted&&result.error==ERROR_INVALID_STATE&&api.styles==0,"maximized entry is explicitly rejected before mutation");
    state={};api=FakeWindow{};api.current.style|=WS_MINIMIZE;result=Enter(state,api,catalog);
    check(!result.attempted&&api.styles==0,"minimized entry is explicitly rejected before mutation");
    state={};api=FakeWindow{};api.failInspect=1;result=Enter(state,api,catalog);
    check(!result.attempted&&!state.saved&&api.styles==0,"failed snapshot leaves the window untouched");
    state={};api=FakeWindow{};api.failStyle=1;result=Enter(state,api,catalog);
    check(!result.succeeded&&result.rolledBack&&state.mode==Mode::Windowed&&result.error==ERROR_ACCESS_DENIED,"style failure restores original window and preserves the exact error");
    state={};api=FakeWindow{};api.failPlace=1;result=Enter(state,api,catalog);
    check(!result.succeeded&&result.rolledBack&&Equal(api.current.rect,saved.rect),"position failure rolls back the prior style and rectangle");
    state={};api=FakeWindow{};api.ignorePlace=1;result=Enter(state,api,catalog);
    check(!result.succeeded&&!result.verified&&result.rolledBack&&result.error==ERROR_INVALID_STATE,"successful API return with wrong readback is not accepted");
    state={};api=FakeWindow{};api.failInspect=2;result=Enter(state,api,catalog);
    check(!result.succeeded&&result.rolledBack,"failed post-entry readback triggers restoration");
    state={};api=FakeWindow{};api.alwaysFailPlace=true;result=Enter(state,api,catalog);
    check(!result.succeeded&&!result.rolledBack&&state.saved&&state.mode==Mode::Recovery,"failed rollback retains saved state and a usable return action");
    api.alwaysFailPlace=false;result=Exit(state,api,catalog);check(result.succeeded&&!state.saved,"return can retry after a failed rollback");
    state={};api=FakeWindow{};api.reenter=&state;result=Enter(state,api,catalog);
    check(result.succeeded&&api.nestedError==ERROR_BUSY,"native callback reentry cannot start a nested transition");
    variant=catalog;variant.monitors[1].bounds={-1920,0,0,1080};variant.monitors[1].work={-1920,0,0,1040};
    result=Refit(state,api,variant);check(result.succeeded&&Equal(api.current.rect,variant.monitors[1].bounds),"topology update uses fresh native bounds without scaling absolute coordinates");
    check(Equal(state.original.rect,saved.rect),"topology refit does not overwrite original window geometry");
    variant.monitors[1].token=7;const auto beforePlaces=api.places;result=Refit(state,api,variant);
    check(!result.attempted&&result.error==ERROR_NOT_FOUND&&api.places==beforePlaces,"lost or replaced target is not silently reselected");
    result=Exit(state,api,variant);check(result.succeeded,"lost target still permits return to a visible ordinary window");
    state={};api=FakeWindow{};Enter(state,api,catalog);api.failPlace=api.places+1;result=Exit(state,api,catalog);
    check(!result.succeeded&&state.saved&&state.mode==Mode::Recovery,"failed exit retains original restoration data");
    result=Exit(state,api,catalog);check(result.succeeded&&!state.saved,"failed exit can be retried without reconnecting");
    state={};api=FakeWindow{};Enter(state,api,catalog);result=Exit(state,api,Catalog{});
    check(!result.attempted&&state.saved&&result.error==ERROR_NOT_READY,"unavailable topology retains return state without inventing visible bounds");
    Shortcut keys;bool exit=false;const LPARAM repeat=static_cast<LPARAM>(static_cast<ULONG_PTR>(1)<<30);
    check(keys.Handle(WM_KEYDOWN,VK_F12,0,true,true,true,true,exit)&&exit,"local initial Ctrl Alt F12 requests return before COM translation");
    check(keys.Handle(WM_KEYDOWN,VK_F12,repeat,true,false,true,true,exit)&&!exit,"held shortcut repeats remain consumed after leaving presentation");
    check(keys.Handle(WM_KEYUP,VK_F12,0,true,false,false,false,exit)&&!exit&&!keys.swallowing,"matching F12 release is consumed after exit");
    check(!keys.Handle(WM_KEYDOWN,VK_F12,0,true,true,false,false,exit),"ordinary F12 down remains available to the child");
    check(!keys.Handle(WM_KEYDOWN,VK_F12,repeat,true,true,true,true,exit)&&!exit,"adding modifiers during a forwarded F12 repeat cannot orphan its down event");
    check(!keys.Handle(WM_KEYUP,VK_F12,0,true,true,true,true,exit),"forwarded F12 keeps its matching keyup");
    check(!keys.Handle(WM_KEYDOWN,VK_F12,0,false,true,true,true,exit),"background viewer cannot activate a local shortcut");
    check(!keys.Handle(WM_KEYDOWN,VK_F12,0,true,false,true,true,exit),"windowed viewer does not reserve the shortcut");
    check(!keys.Handle(WM_KEYDOWN,VK_CONTROL,0,true,true,true,true,exit)&&!keys.Handle(WM_KEYUP,VK_CONTROL,0,true,false,false,true,exit),"Control down and up are both forwarded normally");
    check(!keys.Handle(WM_SYSKEYDOWN,VK_MENU,0,true,true,true,true,exit)&&!keys.Handle(WM_SYSKEYUP,VK_MENU,0,true,false,true,false,exit),"Alt down and up are both forwarded normally");
    check(keys.Handle(WM_SYSKEYDOWN,VK_F12,0,true,true,true,true,exit)&&exit,"system-key variant activates the same local return");
    check(keys.Handle(WM_SYSKEYUP,VK_F12,0,false,false,false,false,exit),"paired release stays consumed if focus changed after the shortcut");
    keys.Handle(WM_KEYDOWN,VK_F12,0,true,true,true,true,exit);
    check(!keys.Handle(WM_KEYDOWN,VK_F12,0,true,false,false,false,exit)&&!keys.swallowing,"fresh press recovers if earlier release went to another application");
    check(!keys.Handle(WM_CHAR,VK_F12,0,true,true,true,true,exit),"character messages are not mistaken for function-key presses");
    std::cout<<"Desktop presentation: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
