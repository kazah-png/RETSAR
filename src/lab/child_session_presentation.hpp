#pragma once
#include <windows.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <vector>

namespace seats::lab::child::presentation {
struct Monitor { std::uintptr_t token=0;RECT bounds{},work{};bool primary=false; };
struct Catalog { bool complete=false;std::vector<Monitor> monitors; };
inline bool Equal(const RECT& a,const RECT& b)noexcept{return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
inline bool Valid(const RECT& r)noexcept {
    const auto w=static_cast<std::int64_t>(r.right)-r.left,h=static_cast<std::int64_t>(r.bottom)-r.top;
    return w>0&&h>0&&w<=INT_MAX&&h<=INT_MAX;
}
inline bool Within(const RECT& inner,const RECT& outer)noexcept{return inner.left>=outer.left&&inner.top>=outer.top&&inner.right<=outer.right&&inner.bottom<=outer.bottom;}
inline DWORD Validate(const Catalog& catalog)noexcept {
    if(!catalog.complete||catalog.monitors.empty()||catalog.monitors.size()>64)return ERROR_NOT_READY;
    unsigned primaries=0;
    for(size_t i=0;i<catalog.monitors.size();++i){const auto& m=catalog.monitors[i];
        if(!m.token||!Valid(m.bounds)||!Valid(m.work)||!Within(m.work,m.bounds))return ERROR_INVALID_DATA;
        if(m.primary)++primaries;
        for(size_t j=0;j<i;++j)if(m.token==catalog.monitors[j].token||Equal(m.bounds,catalog.monitors[j].bounds))return ERROR_DUP_NAME;
    }
    return primaries==1?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
struct Selection { bool found=false;Monitor monitor{};DWORD error=ERROR_NOT_READY; };
inline Selection Secondary(const Catalog& catalog)noexcept {
    Selection out;out.error=Validate(catalog);if(out.error)return out;
    unsigned count=0;for(const auto& m:catalog.monitors)if(!m.primary){out.monitor=m;++count;}
    if(count!=1){out.error=count?ERROR_MORE_DATA:ERROR_NOT_FOUND;return out;}
    out.found=true;return out;
}
inline bool SameTarget(const Catalog& catalog,const Monitor& target,Monitor& fresh)noexcept {
    const auto selected=Secondary(catalog);
    if(!selected.found||selected.monitor.token!=target.token)return false;
    fresh=selected.monitor;return true;
}
struct Snapshot { LONG_PTR style=0;RECT rect{},work{}; };
inline LONG_PTR Borderless(LONG_PTR style)noexcept{return (style&~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW))|WS_POPUP;}
// The saved rectangle is in screen pixels, never WINDOWPLACEMENT workspace
// coordinates. Reuse it exactly if its previous work area still exists.
inline bool RestoreRect(const Snapshot& saved,const Catalog& catalog,RECT& result)noexcept {
    if(!Valid(saved.rect))return false;
    for(const auto& m:catalog.monitors)if(m.token&&Valid(m.bounds)&&Valid(m.work)&&Within(m.work,m.bounds)&&Equal(m.work,saved.work)){result=saved.rect;return true;}
    const Monitor* chosen=nullptr;std::int64_t best=-1;
    for(const auto& m:catalog.monitors){if(!m.token||!Valid(m.bounds)||!Valid(m.work)||!Within(m.work,m.bounds))continue;
        const auto w=std::max<std::int64_t>(0,static_cast<std::int64_t>(std::min(saved.rect.right,m.work.right))-std::max(saved.rect.left,m.work.left));
        const auto h=std::max<std::int64_t>(0,static_cast<std::int64_t>(std::min(saved.rect.bottom,m.work.bottom))-std::max(saved.rect.top,m.work.top));
        const auto area=w*h;if(!chosen||area>best||(area==best&&m.primary)){chosen=&m;best=area;}
    }
    if(!chosen)return false;
    const auto& work=chosen->work;
    const LONG width=static_cast<LONG>(std::min<std::int64_t>(static_cast<std::int64_t>(saved.rect.right)-saved.rect.left,static_cast<std::int64_t>(work.right)-work.left));
    const LONG height=static_cast<LONG>(std::min<std::int64_t>(static_cast<std::int64_t>(saved.rect.bottom)-saved.rect.top,static_cast<std::int64_t>(work.bottom)-work.top));
    const LONG left=std::max(work.left,std::min(saved.rect.left,work.right-width));
    const LONG top=std::max(work.top,std::min(saved.rect.top,work.bottom-height));
    result={left,top,left+width,top+height};return true;
}
enum class Mode { Windowed, Presented, Recovery };
inline const char* Name(Mode mode)noexcept{return mode==Mode::Presented?"presented":mode==Mode::Recovery?"recovery":"windowed";}
struct State { Mode mode=Mode::Windowed;bool busy=false,saved=false;Snapshot original{};Monitor target{}; };
struct Result { bool attempted=false,succeeded=false,verified=false,rolledBack=false,actualKnown=false;DWORD error=ERROR_SUCCESS;Snapshot actual{};RECT requested{};Mode mode=Mode::Windowed; };
struct WindowApi {
    virtual ~WindowApi()=default;
    virtual bool Inspect(Snapshot&,DWORD&)noexcept=0;
    virtual bool Style(LONG_PTR,DWORD&)noexcept=0;
    virtual bool Place(const RECT&,DWORD&)noexcept=0;
};
inline bool Apply(WindowApi& api,LONG_PTR style,const RECT& rect,Result& out)noexcept {
    out.requested=rect;DWORD error=ERROR_SUCCESS;
    const bool styled=api.Style(style,error);if(!styled)out.error=error?error:ERROR_GEN_FAILURE;
    const bool placed=styled&&api.Place(rect,error);if(styled&&!placed)out.error=error?error:ERROR_GEN_FAILURE;
    out.actualKnown=api.Inspect(out.actual,error);
    if(!out.actualKnown&&out.error==ERROR_SUCCESS)out.error=error?error:ERROR_GEN_FAILURE;
    out.verified=out.actualKnown&&out.actual.style==style&&Equal(out.actual.rect,rect);
    if(!out.verified&&out.error==ERROR_SUCCESS)out.error=ERROR_INVALID_STATE;
    out.succeeded=styled&&placed&&out.verified;return out.succeeded;
}
inline Result Enter(State& state,WindowApi& api,const Catalog& catalog)noexcept {
    Result out;out.mode=state.mode;
    if(state.busy||state.mode!=Mode::Windowed){out.error=ERROR_BUSY;return out;}
    const auto selected=Secondary(catalog);if(!selected.found){out.error=selected.error;return out;}
    if(!api.Inspect(state.original,out.error)||!Valid(state.original.rect)){if(!out.error)out.error=ERROR_INVALID_DATA;return out;}
    // Minimised/maximised geometry has separate Windows placement semantics.
    // Require a normal window so restoration never activates it or corrupts its
    // saved normal position. This is an explicit, non-mutating admission check.
    if(state.original.style&(WS_MINIMIZE|WS_MAXIMIZE)){out.error=ERROR_INVALID_STATE;return out;}
    state.saved=true;state.target=selected.monitor;state.busy=true;out.attempted=true;
    if(Apply(api,Borderless(state.original.style),selected.monitor.bounds,out))state.mode=Mode::Presented;
    else {Result rollback;if(Apply(api,state.original.style,state.original.rect,rollback)){out.rolledBack=true;state.mode=Mode::Windowed;state.saved=false;}
        else state.mode=Mode::Recovery;out.actual=rollback.actual;out.actualKnown=rollback.actualKnown;out.verified=false;}
    state.busy=false;out.mode=state.mode;return out;
}
inline Result Exit(State& state,WindowApi& api,const Catalog& catalog)noexcept {
    Result out;out.mode=state.mode;
    if(state.busy){out.error=ERROR_BUSY;return out;}
    if(!state.saved||state.mode==Mode::Windowed){out.error=ERROR_INVALID_STATE;return out;}
    RECT restore{};
    if(!RestoreRect(state.original,catalog,restore)){out.error=ERROR_NOT_READY;return out;}
    state.busy=true;out.attempted=true;
    if(Apply(api,state.original.style,restore,out)){state.mode=Mode::Windowed;state.saved=false;}
    else state.mode=Mode::Recovery;
    state.busy=false;out.mode=state.mode;return out;
}
inline Result Refit(State& state,WindowApi& api,const Catalog& catalog)noexcept {
    Result out;out.mode=state.mode;Monitor target{};
    if(state.busy){out.error=ERROR_BUSY;return out;}
    if(state.mode!=Mode::Presented||!SameTarget(catalog,state.target,target)){out.error=ERROR_NOT_FOUND;return out;}
    state.busy=true;out.attempted=true;
    if(Apply(api,Borderless(state.original.style),target.bounds,out))state.target=target;else state.mode=Mode::Recovery;
    state.busy=false;out.mode=state.mode;return out;
}
struct Shortcut {
    bool swallowing=false;
    // Only messages addressed to this viewer are supplied. Ctrl/Alt themselves
    // still reach the child normally, so their existing down/up pairs balance.
    bool Handle(UINT message,WPARAM key,LPARAM detail,bool active,bool presented,bool ctrl,bool alt,bool& exit)noexcept {
        exit=false;if(key!=VK_F12)return false;
        const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN,up=message==WM_KEYUP||message==WM_SYSKEYUP;
        if(!down&&!up)return false;
        if(up&&swallowing){swallowing=false;return true;}
        if(!down)return false;
        const bool repeat=(static_cast<ULONG_PTR>(detail)&(static_cast<ULONG_PTR>(1)<<30))!=0;
        if(swallowing&&repeat)return true;
        if(!repeat)swallowing=false; // Recover if release went to another app.
        if(!repeat&&active&&presented&&ctrl&&alt){swallowing=true;exit=true;return true;}
        return false;
    }
};
} // namespace seats::lab::child::presentation
