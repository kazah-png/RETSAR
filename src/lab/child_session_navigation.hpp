#pragma once
#include "child_session_activex.hpp"
#include <cstddef>

namespace seats::lab::child::navigation {
enum class Action { None, First, Next, Previous, Activate, Desktop };
enum class Target { Toolbar, Desktop };
struct Button { HWND window=nullptr;bool available=false; };
inline HWND First(const Button* buttons,std::size_t count)noexcept {
    if(!buttons||count>16)return nullptr;
    for(std::size_t i=0;i<count;++i)if(buttons[i].window&&buttons[i].available)return buttons[i].window;
    return nullptr;
}
inline HWND Next(const Button* buttons,std::size_t count,HWND current,bool reverse)noexcept {
    if(!buttons||!count||count>16)return nullptr;
    std::size_t origin=count;for(std::size_t i=0;i<count;++i)if(buttons[i].window==current){origin=i;break;}
    if(origin==count)return First(buttons,count);
    for(std::size_t offset=1;offset<=count;++offset){const auto index=reverse?(origin+count-offset)%count:(origin+offset)%count;
        if(buttons[index].window&&buttons[index].available)return buttons[index].window;}
    return nullptr;
}
// DISPID 23 supplies one INT. Direction is intentionally not interpreted: the
// public documentation repeats 1 for both directions. Unknown values still
// choose the first available toolbar button, never a guessed reverse mapping.
inline bool FocusReleased(const DISPPARAMS* args)noexcept {
    return args&&args->cArgs==1&&args->cNamedArgs==0&&args->rgvarg&&args->rgvarg[0].vt==VT_I4;
}
struct KeyRoute { bool consumed=false;Action action=Action::None; };
struct Keys {
    bool paired[3]{};
    KeyRoute Handle(UINT message,WPARAM key,LPARAM details,bool owned,bool foreground,bool toolbar,
        bool shift,bool control,bool alt)noexcept {
        if(!owned)return {};
        const int index=key==VK_TAB?0:key==VK_RETURN?1:key==VK_ESCAPE?2:-1;
        if(index<0)return {};
        const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
        const bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
        if(up&&paired[index]){paired[index]=false;return {true,Action::None};}
        if(!down)return {};
        const bool repeat=(static_cast<ULONG_PTR>(details)&(static_cast<ULONG_PTR>(1)<<30))!=0;
        if(paired[index]&&repeat)return {true,Action::None};
        if(!repeat)paired[index]=false;
        if(repeat||!foreground||!toolbar||control||alt||(shift&&key!=VK_TAB))return {};
        paired[index]=true;
        return {true,key==VK_TAB?(shift?Action::Previous:Action::Next):key==VK_RETURN?Action::Activate:Action::Desktop};
    }
};
struct WindowApi {
    virtual ~WindowApi()=default;
    virtual bool ForegroundOwned()noexcept=0;
    virtual HWND CurrentFocus()noexcept=0;
    virtual bool OwnedChild(HWND)noexcept=0;
    virtual bool Available(HWND)noexcept=0;
    virtual bool Toolbar(HWND)noexcept=0;
    virtual bool Descendant(HWND parent,HWND child)noexcept=0;
    // Return a Win32 error only if SetFocus failed. A NULL previous focus alone
    // is not failure; independent readback is always required below.
    virtual DWORD Set(HWND)noexcept=0;
};
struct Result { HRESULT result=E_PENDING;Target target=Target::Toolbar;bool attempted=false,verified=false; };
inline bool Cancelled(const bool* value)noexcept{return value&&*value;}
inline bool SameGeneration(const unsigned long long* value,unsigned long long expected)noexcept{return !value||*value==expected;}
inline bool DesktopWindow(WindowApi& api,HWND target)noexcept {
    return target&&api.OwnedChild(target)&&api.Available(target)&&!api.Toolbar(target);
}
inline bool DesktopFocus(WindowApi& api,HWND target)noexcept {
    const HWND actual=api.CurrentFocus();
    return actual&&api.OwnedChild(actual)&&!api.Toolbar(actual)&&(actual==target||api.Descendant(target,actual));
}
inline Result FocusButton(WindowApi& api,HWND target,unsigned& depth,const bool* cancelled=nullptr)noexcept {
    Result out;if(depth)return out;
    ax::OutboundScope guard(&depth);
    if(Cancelled(cancelled)||!api.ForegroundOwned()){out.result=E_ABORT;return out;}
    if(!target||!api.Toolbar(target)||!api.OwnedChild(target)||!api.Available(target)){out.result=E_INVALIDARG;return out;}
    DWORD error=ERROR_SUCCESS;
    if(api.CurrentFocus()!=target){out.attempted=true;error=api.Set(target);}
    if(Cancelled(cancelled)||!api.ForegroundOwned()){out.result=E_ABORT;return out;}
    out.verified=!error&&api.Toolbar(target)&&api.OwnedChild(target)&&api.Available(target)&&api.CurrentFocus()==target;
    out.result=out.verified?S_OK:HRESULT_FROM_WIN32(error?error:ERROR_NOT_READY);return out;
}
inline Result FocusDesktop(IOleInPlaceActiveObject* object,WindowApi& api,unsigned& depth,
    const bool* cancelled=nullptr,const unsigned long long* generation=nullptr,unsigned long long expected=0)noexcept {
    Result out;out.target=Target::Desktop;
    if(!object){out.result=E_POINTER;return out;}
    if(depth)return out;
    ax::OutboundScope guard(&depth);
    const auto blocked=[&]()noexcept{return Cancelled(cancelled)||!SameGeneration(generation,expected)||!api.ForegroundOwned();};
    if(blocked()){out.result=E_ABORT;return out;}
    object->AddRef();
    HWND target=nullptr;
    if(blocked())out.result=E_ABORT;
    else {
        out.result=object->GetWindow(&target);
        if(out.result==S_OK){
            if(blocked())out.result=E_ABORT;
            else if(!DesktopWindow(api,target))out.result=E_INVALIDARG;
            else {
                DWORD error=ERROR_SUCCESS;
                if(!DesktopFocus(api,target)){out.attempted=true;error=api.Set(target);}
                out.result=error?HRESULT_FROM_WIN32(error):S_OK;
            }
        }else if(SUCCEEDED(out.result))out.result=E_UNEXPECTED;
    }
    // Release is guarded too; a callback here can invalidate the focus target.
    object->Release();
    if(blocked())out.result=E_ABORT;
    if(out.result==S_OK){
        out.verified=DesktopWindow(api,target)&&DesktopFocus(api,target);
        if(!out.verified)out.result=HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    return out;
}
} // namespace seats::lab::child::navigation
