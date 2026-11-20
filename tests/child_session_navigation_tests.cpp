#include "../src/lab/child_session_navigation.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
using namespace seats::lab::child;
namespace {
unsigned passed=0,failed=0;
void Check(bool value,const char* label){if(value)++passed;else{++failed;std::cerr<<"FAIL "<<label<<'\n';}}
HWND Window(std::uintptr_t value)noexcept{return reinterpret_cast<HWND>(value);}
constexpr LPARAM Repeated=static_cast<LPARAM>(static_cast<ULONG_PTR>(1)<<30);
struct Node {HWND window=nullptr,parent=nullptr;bool owned=true,available=true,toolbar=false;};
class Windows final:public navigation::WindowApi {
public:
    unsigned& depth;unsigned calls=0,sets=0;bool guarded=true,foreground=true,ignoreSet=false;
    HWND focus=nullptr;DWORD error=ERROR_SUCCESS;std::function<void()> onSet;
    std::array<Node,6> nodes{{{Window(1),nullptr,true,true,true},{Window(2),nullptr,true,true,true},
        {Window(3),nullptr,true,true,false},{Window(4),Window(3),true,true,false},
        {Window(5),Window(3),false,true,false},{Window(6),nullptr,true,true,false}}};
    explicit Windows(unsigned& value):depth(value){}
    void Touch()noexcept{++calls;guarded=guarded&&depth==1;}
    Node* Find(HWND window)noexcept{for(auto& node:nodes)if(node.window==window)return &node;return nullptr;}
    bool ForegroundOwned()noexcept override{Touch();return foreground;}
    HWND CurrentFocus()noexcept override{Touch();return focus;}
    bool OwnedChild(HWND window)noexcept override{Touch();const auto* node=Find(window);return node&&node->owned;}
    bool Available(HWND window)noexcept override{Touch();const auto* node=Find(window);return node&&node->available;}
    bool Toolbar(HWND window)noexcept override{Touch();const auto* node=Find(window);return node&&node->toolbar;}
    bool Descendant(HWND parent,HWND child)noexcept override{Touch();const auto* node=Find(child);return node&&node->parent==parent;}
    DWORD Set(HWND window)noexcept override{Touch();++sets;if(!ignoreSet)focus=window;if(onSet)onSet();return error;}
};
class Object final:public IOleInPlaceActiveObject {
public:
    unsigned& depth;ULONG refs=1;unsigned adds=0,gets=0,releases=0;bool guarded=true;
    HWND window=Window(3);HRESULT result=S_OK;std::function<void()> onAdd,onGet,onRelease;
    explicit Object(unsigned& value):depth(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{++adds;guarded=guarded&&depth==1&&refs==1;++refs;if(onAdd)onAdd();return refs;}
    ULONG STDMETHODCALLTYPE Release()override{++releases;guarded=guarded&&depth==1&&refs==2;if(onRelease)onRelease();return --refs;}
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out)override{++gets;guarded=guarded&&depth==1&&refs==2;if(out)*out=window;if(onGet)onGet();return result;}
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT,IOleInPlaceUIWindow*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL)override{return E_NOTIMPL;}
};
void ListsAndEvent(){
    using namespace navigation;
    std::array<Button,5> buttons{{{Window(1),false},{nullptr,true},{Window(2),true},{Window(3),false},{Window(4),true}}};
    Check(!First(nullptr,1)&&!Next(nullptr,1,Window(2),false),"missing toolbar list cannot choose focus");
    Check(!First(buttons.data(),0)&&!Next(buttons.data(),0,Window(2),false),"empty toolbar is inert");
    Check(!First(buttons.data(),17)&&!Next(buttons.data(),17,Window(2),false),"oversized list rejected before dereference");
    Check(First(buttons.data(),buttons.size())==Window(2),"first skips missing and unavailable buttons");
    Check(Next(buttons.data(),buttons.size(),Window(2),false)==Window(4),"forward skips unavailable buttons");
    Check(Next(buttons.data(),buttons.size(),Window(4),false)==Window(2),"forward wraps to first available");
    Check(Next(buttons.data(),buttons.size(),Window(4),true)==Window(2),"reverse skips unavailable buttons");
    Check(Next(buttons.data(),buttons.size(),Window(2),true)==Window(4),"reverse wraps to last available");
    Check(Next(buttons.data(),buttons.size(),Window(3),false)==Window(4),"disabled current position does not trap navigation");
    Check(Next(buttons.data(),buttons.size(),Window(99),true)==Window(2),"unknown current safely selects first available");
    buttons[4].available=false;
    Check(Next(buttons.data(),buttons.size(),Window(2),false)==Window(2)&&
        Next(buttons.data(),buttons.size(),Window(2),true)==Window(2),"single available button remains selectable in either direction");
    buttons[2].available=false;
    Check(!First(buttons.data(),buttons.size())&&!Next(buttons.data(),buttons.size(),Window(2),false),"no available control yields no focus target");
    std::array<Button,16> boundary{};boundary.back()={Window(16),true};
    Check(First(boundary.data(),boundary.size())==Window(16),"sixteen-control bound is inclusive");
    VARIANT value{};value.vt=VT_I4;DISPPARAMS args{&value,nullptr,1,0};
    for(const LONG direction:{-1L,0L,1L,2L,static_cast<LONG>(MAXLONG)}){value.lVal=direction;Check(FocusReleased(&args),"typed focus release accepts direction without guessing undocumented mapping");}
    Check(!FocusReleased(nullptr),"missing event arguments rejected");
    args.cArgs=0;Check(!FocusReleased(&args),"empty event rejected");
    args.cArgs=2;Check(!FocusReleased(&args),"extra event argument rejected");
    args.cArgs=1;args.cNamedArgs=1;Check(!FocusReleased(&args),"named event argument rejected");
    args.cNamedArgs=0;args.rgvarg=nullptr;Check(!FocusReleased(&args),"missing event value rejected");args.rgvarg=&value;
    for(const VARTYPE type:{static_cast<VARTYPE>(VT_UI4),static_cast<VARTYPE>(VT_I2),static_cast<VARTYPE>(VT_BOOL),
        static_cast<VARTYPE>(VT_I4|VT_BYREF),static_cast<VARTYPE>(VT_EMPTY)}){
        value.vt=type;Check(!FocusReleased(&args),"noncanonical focus-release argument type rejected");
    }
}
void KeyRoutes(){
    using namespace navigation;
    for(const UINT down:{WM_KEYDOWN,WM_SYSKEYDOWN})for(const WPARAM key:{static_cast<WPARAM>(VK_TAB),static_cast<WPARAM>(VK_RETURN),static_cast<WPARAM>(VK_ESCAPE)}){
        Keys keys;const auto expected=key==VK_TAB?Action::Next:key==VK_RETURN?Action::Activate:Action::Desktop;
        auto result=keys.Handle(down,key,0,true,true,true,false,false,false);
        Check(result.consumed&&result.action==expected,"toolbar key down emits exactly its local action");
        result=keys.Handle(down,key,Repeated,true,true,true,false,false,false);
        Check(result.consumed&&result.action==Action::None,"paired auto-repeat is consumed without repeating action");
        const UINT up=down==WM_KEYDOWN?WM_SYSKEYUP:WM_KEYUP;
        result=keys.Handle(up,key,0,true,false,false,true,true,true);
        Check(result.consumed&&result.action==Action::None,"owned paired release stays consumed after focus or modifier changes");
        Check(!keys.Handle(up,key,0,true,true,true,false,false,false).consumed,"duplicate release is not retained forever");
    }
    Keys reverse;const auto previous=reverse.Handle(WM_KEYDOWN,VK_TAB,0,true,true,true,true,false,false);
    Check(previous.consumed&&previous.action==Action::Previous,"shift tab traverses toolbar in reverse");
    Check(reverse.Handle(WM_KEYUP,VK_TAB,0,true,true,false,false,false,false).consumed,"shift tab release follows the consumed down outside toolbar");
    for(const WPARAM key:{static_cast<WPARAM>(VK_TAB),static_cast<WPARAM>(VK_RETURN),static_cast<WPARAM>(VK_ESCAPE)}){
        for(unsigned reason=0;reason<7;++reason){
            Keys keys;const bool owned=reason!=0,foreground=reason!=1,toolbar=reason!=2;
            const bool shift=reason==5,control=reason==3,alt=reason==4;
            const LPARAM details=reason==6?Repeated:0;
            if(shift&&key==VK_TAB)continue;
            const auto result=keys.Handle(WM_KEYDOWN,key,details,owned,foreground,toolbar,shift,control,alt);
            Check(!result.consumed&&result.action==Action::None,"foreign desktop inactive modified or unmatched repeat key is not intercepted");
            Check(!keys.Handle(WM_KEYUP,key,0,true,true,true,false,false,false).consumed,"unhandled down does not consume a later release");
        }
    }
    Keys pair;pair.Handle(WM_KEYDOWN,VK_ESCAPE,0,true,true,true,false,false,false);
    Check(!pair.Handle(WM_KEYUP,VK_ESCAPE,0,false,true,true,false,false,false).consumed,"foreign-window key up is never intercepted");
    Check(pair.Handle(WM_KEYUP,VK_ESCAPE,0,true,false,false,false,false,false).consumed,"foreign release cannot erase the retained own release");
    Keys lost;lost.Handle(WM_KEYDOWN,VK_RETURN,0,true,true,true,false,false,false);
    auto repeated=lost.Handle(WM_KEYDOWN,VK_RETURN,Repeated,true,false,false,false,false,false);
    Check(repeated.consumed&&repeated.action==Action::None,"held local key does not start repeating inside desktop after focus loss");
    Check(!lost.Handle(WM_KEYDOWN,VK_RETURN,0,true,true,false,false,false,false).consumed,"new nonrepeat desktop down clears a lost old toolbar pair");
    Check(!lost.Handle(WM_KEYUP,VK_RETURN,0,true,true,false,false,false,false).consumed,"release of the new desktop down is preserved");
    Keys chars;chars.Handle(WM_KEYDOWN,VK_TAB,0,true,true,true,false,false,false);
    Check(!chars.Handle(WM_CHAR,VK_TAB,0,true,true,true,false,false,false).consumed,"character messages are outside physical-key pairing");
    Check(!chars.Handle(WM_KEYUP,VK_RETURN,0,true,true,true,false,false,false).consumed,"pair ownership is specific to its key");
    Check(chars.Handle(WM_KEYUP,VK_TAB,0,true,true,true,false,false,false).consumed,"unrelated messages preserve the original key pair");
    Keys ordinary;
    for(const UINT message:{WM_KEYDOWN,WM_KEYUP,WM_SYSKEYDOWN,WM_SYSKEYUP,WM_PAINT})
        Check(!ordinary.Handle(message,L'A',0,true,true,true,false,false,false).consumed,"ordinary keys and unrelated messages retain their route");
}
void ButtonFocus(){
    using namespace navigation;
    {
        unsigned depth=2;Windows api(depth);const auto result=FocusButton(api,Window(1),depth);
        Check(result.result==E_PENDING&&!result.attempted&&!result.verified&&api.calls==0&&depth==2,"nested toolbar focus is deferred without API calls");
    }
    for(unsigned condition=0;condition<6;++condition){
        unsigned depth=0;Windows api(depth);bool cancelled=condition==0;HWND target=Window(1);
        if(condition==1)api.foreground=false;else if(condition==2)target=nullptr;
        else if(condition==3)api.nodes[0].owned=false;else if(condition==4)api.nodes[0].available=false;
        else if(condition==5)target=Window(3);
        const auto result=FocusButton(api,target,depth,&cancelled);
        Check(result.result==(condition<2?E_ABORT:E_INVALIDARG)&&!result.verified&&!result.attempted&&api.sets==0,
            "toolbar focus rejects cancellation inactive context or invalid target before Set");
        Check(api.guarded&&depth==0,"toolbar rejection restores outbound guard");
    }
    {
        unsigned depth=0;Windows api(depth);const auto result=FocusButton(api,Window(1),depth);
        Check(result.result==S_OK&&result.target==Target::Toolbar&&result.attempted&&result.verified&&api.focus==Window(1),
            "null former focus is compatible with independently verified successful Set");
        Check(api.guarded&&depth==0&&api.sets==1,"toolbar Set and readback remain inside guard");
        const auto same=FocusButton(api,Window(1),depth);
        Check(same.result==S_OK&&same.verified&&!same.attempted&&api.sets==1,"already focused toolbar target is verified without Set");
    }
    for(unsigned change=0;change<8;++change){
        unsigned depth=0;Windows api(depth);bool cancelled=false;
        if(change==0)api.error=ERROR_ACCESS_DENIED;
        else if(change==1)api.ignoreSet=true;
        else api.onSet=[&](){if(change==2)cancelled=true;else if(change==3)api.foreground=false;
            else if(change==4)api.nodes[0].owned=false;else if(change==5)api.nodes[0].available=false;
            else if(change==6)api.nodes[0].toolbar=false;else api.focus=Window(5);};
        const auto result=FocusButton(api,Window(1),depth,&cancelled);
        const HRESULT expected=change==0?HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED):change==2||change==3?E_ABORT:HRESULT_FROM_WIN32(ERROR_NOT_READY);
        Check(result.result==expected&&result.attempted&&!result.verified&&api.sets==1,"toolbar focus rejects failed ignored or invalidated Set despite apparent success");
        Check(api.guarded&&depth==0,"failed toolbar readback keeps guard balanced");
    }
    {
        unsigned depth=0;Windows api(depth);HRESULT nested=S_OK;
        api.onSet=[&](){nested=FocusButton(api,Window(2),depth).result;};
        const auto result=FocusButton(api,Window(1),depth);
        Check(result.result==S_OK&&nested==E_PENDING&&api.sets==1&&api.guarded&&depth==0,"Set reentry cannot focus a second toolbar button");
    }
}
void DesktopFocus(){
    using namespace navigation;
    {
        unsigned depth=0;Windows api(depth);const auto result=FocusDesktop(nullptr,api,depth);
        Check(result.result==E_POINTER&&result.target==Target::Desktop&&!result.verified&&api.calls==0&&depth==0,"missing active object cannot focus a desktop");
    }
    {
        unsigned depth=3;Windows api(depth);Object object(depth);const auto result=FocusDesktop(&object,api,depth);
        Check(result.result==E_PENDING&&object.adds==0&&api.calls==0&&depth==3,"busy desktop focus does not enter COM or window APIs");
    }
    for(unsigned reason=0;reason<3;++reason){
        unsigned depth=0;Windows api(depth);Object object(depth);bool cancelled=reason==0;unsigned long long generation=reason==2?2:1;
        if(reason==1)api.foreground=false;
        const auto result=FocusDesktop(&object,api,depth,&cancelled,&generation,1);
        Check(result.result==E_ABORT&&!result.attempted&&!result.verified&&object.adds==0&&object.gets==0&&api.sets==0&&depth==0,
            "initial cancellation foreground or object generation blocks all COM lifetime calls");
    }
    for(const HRESULT status:{E_ACCESSDENIED,S_FALSE}){
        unsigned depth=0;Windows api(depth);Object object(depth);object.result=status;
        const auto result=FocusDesktop(&object,api,depth);
        Check(result.result==(FAILED(status)?status:E_UNEXPECTED)&&!result.attempted&&!result.verified&&api.sets==0,
            "GetWindow failure and noncanonical success do not authorize focus");
        Check(object.adds==1&&object.gets==1&&object.releases==1&&object.refs==1&&object.guarded&&api.guarded&&depth==0,
            "GetWindow failure balances guarded COM reference lifetime");
    }
    for(unsigned invalid=0;invalid<5;++invalid){
        unsigned depth=0;Windows api(depth);Object object(depth);
        if(invalid==0)object.window=nullptr;else if(invalid==1)object.window=Window(99);
        else if(invalid==2)object.window=Window(5);else if(invalid==3)api.nodes[2].available=false;else object.window=Window(1);
        const auto result=FocusDesktop(&object,api,depth);
        Check(result.result==E_INVALIDARG&&!result.attempted&&!result.verified&&api.sets==0,"COM window must be an available owned non-toolbar child");
        Check(object.refs==1&&object.releases==1&&object.guarded&&api.guarded&&depth==0,"invalid COM window still releases inside guard");
    }
    for(const HWND initial:{Window(3),Window(4),Window(5),Window(6),Window(1),static_cast<HWND>(nullptr)}){
        unsigned depth=0;Windows api(depth);Object object(depth);api.focus=initial;
        const bool already=initial==Window(3)||initial==Window(4);
        const auto result=FocusDesktop(&object,api,depth);
        Check(result.result==S_OK&&result.verified&&result.attempted!=already&&api.sets==(already?0u:1u),
            "only owned desktop root or descendant counts as already focused");
        Check(object.refs==1&&object.adds==1&&object.gets==1&&object.releases==1&&object.guarded&&api.guarded&&depth==0,
            "successful desktop focus retains full guarded COM lifetime");
    }
    for(unsigned change=0;change<4;++change){
        unsigned depth=0;Windows api(depth);Object object(depth);
        if(change==0)api.error=ERROR_ACCESS_DENIED;else if(change==1)api.ignoreSet=true;
        else api.onSet=[&](){api.focus=change==2?Window(4):Window(5);};
        const auto result=FocusDesktop(&object,api,depth);
        const HRESULT expected=change==0?HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED):change==2?S_OK:HRESULT_FROM_WIN32(ERROR_NOT_READY);
        Check(result.result==expected&&result.verified==(change==2)&&result.attempted&&api.sets==1,
            "desktop Set errors ignored changes and foreign descendants cannot masquerade as verified focus");
        Check(object.refs==1&&object.guarded&&api.guarded&&depth==0,"desktop Set outcomes balance all guards");
    }
    for(unsigned stage=0;stage<4;++stage)for(unsigned cause=0;cause<3;++cause){
        unsigned depth=0;Windows api(depth);Object object(depth);bool cancelled=false;unsigned long long generation=1;
        const auto invalidate=[&](){if(cause==0)cancelled=true;else if(cause==1)api.foreground=false;else ++generation;};
        if(stage==0)object.onAdd=invalidate;else if(stage==1)object.onGet=invalidate;
        else if(stage==2)api.onSet=invalidate;else object.onRelease=invalidate;
        const auto result=FocusDesktop(&object,api,depth,&cancelled,&generation,1);
        Check(result.result==E_ABORT&&!result.verified&&result.attempted==(stage>=2)&&api.sets==(stage>=2?1u:0u),
            "cancellation foreground loss or generation change during AddRef GetWindow Set or Release invalidates navigation");
        Check(object.adds==1&&object.releases==1&&object.refs==1&&object.gets==(stage==0?0u:1u)&&object.guarded&&api.guarded&&depth==0,
            "invalidated navigation keeps object retained and reference lifetime guarded");
    }
    for(unsigned stage=0;stage<4;++stage){
        unsigned depth=0;Windows api(depth);Object object(depth);HRESULT nestedDesktop=S_OK,nestedButton=S_OK;
        const auto nested=[&](){nestedDesktop=FocusDesktop(&object,api,depth).result;nestedButton=FocusButton(api,Window(1),depth).result;};
        if(stage==0)object.onAdd=nested;else if(stage==1)object.onGet=nested;else if(stage==2)api.onSet=nested;else object.onRelease=nested;
        const auto result=FocusDesktop(&object,api,depth);
        Check(result.result==S_OK&&result.verified&&nestedDesktop==E_PENDING&&nestedButton==E_PENDING,
            "reentry at every outbound stage defers both toolbar and desktop focus");
        Check(object.adds==1&&object.gets==1&&object.releases==1&&object.refs==1&&api.sets==1&&object.guarded&&api.guarded&&depth==0,
            "nested navigation cannot create extra COM calls Set attempts or leaked references");
    }
    for(unsigned change=0;change<4;++change){
        unsigned depth=0;Windows api(depth);Object object(depth);
        object.onRelease=[&](){if(change==0)api.nodes[2].owned=false;else if(change==1)api.nodes[2].available=false;
            else if(change==2)api.nodes[2].toolbar=true;else api.focus=Window(6);};
        const auto result=FocusDesktop(&object,api,depth);
        Check(result.result==HRESULT_FROM_WIN32(ERROR_NOT_READY)&&!result.verified&&result.attempted&&object.refs==1&&depth==0,
            "Release-time window or focus invalidation defeats earlier successful Set");
    }
}
}
int main(){
    ListsAndEvent();KeyRoutes();ButtonFocus();DesktopFocus();
    std::cout<<"Desktop navigation: "<<passed<<" passed, "<<failed
        <<" failed; fake COM and window APIs only, no windows, focus, input or sessions.\n";
    return failed?1:0;
}
