#pragma once
#include "../lab/child_session_client_mode.hpp"
#include "../lab/child_session_inventory.hpp"
#include <vector>

namespace retsar::desktop {
using SessionRow=seats::lab::child::client::Row;
using seats::lab::child::RequiresUserObservation;

enum class Action { None, ShowViewer, CreateDesktop, ResumeDesktop, ResumeEnabledDesktop };
enum class Availability { Loading, Ready, ReadyToResume, ReadyToResumeEnabled, ViewerOpen, AlreadyActive, FeatureInUse, MissingComponents, Unknown, UnsupportedContext };
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
        const client::Request enabledResume{client::Mode::ResumeChild,state.child.value,true};
        if(client::AcceptSnapshot(enabledResume,state)&&
           client::AcceptInventory(enabledResume,state.caller.value,facts.inventoryComplete,facts.rows.data(),facts.rows.size())) {
            if(!facts.viewerFilePresent||!facts.receiptDirectoryPresent)
                return {Action::None,Availability::MissingComponents,enabledResume.target};
            return {Action::ResumeEnabledDesktop,Availability::ReadyToResumeEnabled,enabledResume.target};
        }
        return {Action::None,Availability::FeatureInUse};
    }
    client::Request request{};
    if(state.child.succeeded&&state.child.value!=MAXDWORD)
        request={client::Mode::ResumeChild,state.child.value};
    if(!client::AcceptSnapshot(request,state)||
       !client::AcceptInventory(request,state.caller.value,facts.inventoryComplete,facts.rows.data(),facts.rows.size()))return {};
    if(!facts.viewerFilePresent||!facts.receiptDirectoryPresent)
        return {Action::None,Availability::MissingComponents,request.target};
    return {request.mode==client::Mode::ResumeChild?Action::ResumeDesktop:Action::CreateDesktop,
        request.mode==client::Mode::ResumeChild?Availability::ReadyToResume:Availability::Ready,request.target};
}
inline bool CanLaunch(const Decision& decision)noexcept {
    return decision.action==Action::CreateDesktop||decision.action==Action::ResumeDesktop||decision.action==Action::ResumeEnabledDesktop;
}
inline bool IsResume(const Decision& decision)noexcept {
    return decision.action==Action::ResumeDesktop||decision.action==Action::ResumeEnabledDesktop;
}
inline const wchar_t* LaunchMode(const Decision& decision)noexcept {
    switch(decision.action) {
    case Action::CreateDesktop:return L"--connect-child";
    case Action::ResumeDesktop:return L"--resume-child-session";
    case Action::ResumeEnabledDesktop:return L"--resume-enabled-child-session";
    default:return nullptr;
    }
}
inline const wchar_t* ActionLabel(const Decision& decision)noexcept {
    switch(decision.action) {
    case Action::ShowViewer:return L"Show desktop";
    case Action::CreateDesktop:return L"Open desktop";
    case Action::ResumeDesktop:case Action::ResumeEnabledDesktop:return L"Resume desktop";
    default:return L"Open desktop";
    }
}
inline const wchar_t* AvailabilityTitle(Availability value)noexcept {
    switch(value) {
    case Availability::Loading:return L"Checking desktops";
    case Availability::Ready:return L"Ready to open";
    case Availability::ReadyToResume:case Availability::ReadyToResumeEnabled:return L"Ready to resume";
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
    case Availability::ReadyToResume:return L"Your existing desktop is disconnected. Resume reconnects to that desktop; Windows may ask you to confirm your identity.";
    case Availability::ReadyToResumeEnabled:return L"Resume your existing disconnected desktop. Its enabled connection setting will be preserved; Windows may ask you to confirm your identity.";
    case Availability::ViewerOpen:return L"The desktop viewer is open. Bring it forward to check its connection or complete Windows identity confirmation.";
    case Availability::AlreadyActive:return L"Your independent desktop is already active. Refresh to update its observed state.";
    case Availability::FeatureInUse:return L"The connection setting is enabled, but no safely resumable desktop was verified. RETSAR will leave it in place.";
    case Availability::MissingComponents:return L"Build the desktop viewer and prepare the development receipt folder. Setup details are available in the guide.";
    case Availability::UnsupportedContext:return L"Open RETSAR Desktop from your main Windows console to prepare another desktop.";
    default:return L"Some session information is unavailable or another sign-in is present. Refresh or inspect Activity before opening a desktop.";
    }
}
// Coalesce return-to-console observations into one posted request. Activation
// during a refresh/launch is retained for a later safe UI-loop boundary. This
// schedules read-only inventory only; it has no connection or retry action.
class ActivationRefresh {
    bool active_=false,pending_=false,posted_=false;
public:
    void SetActive(bool active,bool preview,bool closing)noexcept {
        if(preview||closing){active_=false;pending_=false;return;}
        if(active&&!active_)pending_=true;
        active_=active;
    }
    bool Schedule(bool busy,bool preview,bool closing)noexcept {
        if(preview||closing){pending_=false;return false;}
        if(!active_||!pending_||posted_||busy)return false;
        posted_=true;return true;
    }
    void PostFailed()noexcept{posted_=false;}
    bool Take(bool busy,bool preview,bool closing)noexcept {
        posted_=false;
        if(preview||closing){pending_=false;return false;}
        if(!active_||!pending_||busy)return false;
        pending_=false;return true;
    }
};
inline int BoundedPercent(unsigned long value)noexcept {return value>100?100:static_cast<int>(value);}
}