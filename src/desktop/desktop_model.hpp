#pragma once
#include "../lab/child_session_client_mode.hpp"
#include <vector>

namespace retsar::desktop {
using SessionRow=seats::lab::child::client::Row;
enum class Action { None, ShowViewer, CreateDesktop, ResumeDesktop };
enum class Availability { Loading, Ready, ViewerOpen, AlreadyActive, FeatureInUse, MissingComponents, Unknown, UnsupportedContext };
struct Facts {
    seats::lab::child::Snapshot session{};
    std::vector<SessionRow> rows;
    bool inventoryComplete=false;
    bool viewerFilePresent=false;
    bool receiptDirectoryPresent=false;
    bool verifiedViewerPresent=false;
    bool refreshing=false;
    bool launchPending=false;
    bool designPreview=false;
};
struct Decision {Action action=Action::None;Availability availability=Availability::Unknown;DWORD child=MAXDWORD;};
inline Decision Decide(const Facts& facts) noexcept {
    using namespace seats::lab::child;
    if(facts.refreshing||facts.launchPending)return {Action::None,Availability::Loading};
    if(facts.designPreview)return {Action::None,Availability::Ready};
    if(facts.verifiedViewerPresent)return {Action::ShowViewer,Availability::ViewerOpen};
    const auto& state=facts.session;
    if(!state.apiAvailable||!state.enabled.attempted||!state.enabled.succeeded||
       !state.caller.attempted||!state.caller.succeeded||!state.console.attempted||
       !state.child.attempted||!facts.inventoryComplete)return {};
    if(!state.caller.value||state.caller.value==MAXDWORD||state.caller.value!=state.console.value)
        return {Action::None,Availability::UnsupportedContext};
    if(state.enabled.value) {
        if(state.child.succeeded&&state.child.value!=MAXDWORD&&state.child.value!=state.caller.value)
            for(const auto& row:facts.rows)if(row.id==state.child.value&&row.state==WTSActive&&row.hasUser)
                return {Action::None,Availability::AlreadyActive,state.child.value};
        return {Action::None,Availability::FeatureInUse};
    }
    client::Request request{};
    if(state.child.succeeded&&state.child.value!=MAXDWORD)
        request={client::Mode::ResumeChild,state.child.value};
    if(!client::AcceptSnapshot(request,state)||
       !client::AcceptInventory(request,state.caller.value,facts.inventoryComplete,facts.rows.data(),facts.rows.size()))return {};
    if(!facts.viewerFilePresent||!facts.receiptDirectoryPresent)
        return {Action::None,Availability::MissingComponents,request.target};
    return {request.mode==client::Mode::ResumeChild?Action::ResumeDesktop:Action::CreateDesktop,Availability::Ready,request.target};
}
inline bool CanLaunch(const Decision& decision)noexcept {
    return decision.action==Action::CreateDesktop||decision.action==Action::ResumeDesktop;
}
inline const wchar_t* ActionLabel(const Decision& decision)noexcept {
    switch(decision.action) {
    case Action::ShowViewer:return L"Show desktop";
    case Action::CreateDesktop:return L"Open desktop";
    case Action::ResumeDesktop:return L"Resume desktop";
    default:return L"Open desktop";
    }
}
inline const wchar_t* AvailabilityTitle(Availability value)noexcept {
    switch(value) {
    case Availability::Loading:return L"Checking desktops";
    case Availability::Ready:return L"Ready to open";
    case Availability::ViewerOpen:return L"Viewer is open";
    case Availability::AlreadyActive:return L"Desktop is open";
    case Availability::FeatureInUse:return L"Connection already enabled";
    case Availability::MissingComponents:return L"Development setup needed";
    case Availability::UnsupportedContext:return L"Use your main desktop";
    default:return L"Status needs attention";
    }
}
inline const wchar_t* AvailabilityDetail(Availability value)noexcept {
    switch(value) {
    case Availability::Loading:return L"Reading the current session and display state. Your open applications stay running.";
    case Availability::Ready:return L"Open a separate Windows desktop for your applications. Windows may ask you to confirm your identity.";
    case Availability::ViewerOpen:return L"The desktop viewer is open. Bring it forward to check its connection or complete Windows identity confirmation.";
    case Availability::AlreadyActive:return L"Your independent desktop is already active. Refresh to update its observed state.";
    case Availability::FeatureInUse:return L"An existing connection controls this Windows setting. RETSAR will leave it in place.";
    case Availability::MissingComponents:return L"Build the desktop viewer and prepare the development receipt folder. Setup details are available in the guide.";
    case Availability::UnsupportedContext:return L"Open RETSAR Desktop from your main Windows console to prepare another desktop.";
    default:return L"Some session information is unavailable or another sign-in is present. Refresh or inspect Activity before opening a desktop.";
    }
}
inline int BoundedPercent(unsigned long value)noexcept {return value>100?100:static_cast<int>(value);}
}