#include "physical_display_model.hpp"
#include <Windows.h>
#include <winrt/Windows.Devices.Display.h>
#include <winrt/Windows.Devices.Display.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <iostream>
#include <new>
#include <type_traits>
#include <utility>

namespace {
using namespace retsar::lab::physical_display;
using namespace winrt::Windows::Devices::Display::Core;
constexpr std::uint32_t MaximumTargets=128;
template<class Action> Call ObserveCall(Action&& action)noexcept {
    Call result{true,false,0};
    try{action();result.succeeded=true;}
    catch(const winrt::hresult_error& error){result.hresult=static_cast<std::int32_t>(error.code());}
    catch(const std::bad_alloc&){result.hresult=static_cast<std::int32_t>(E_OUTOFMEMORY);}
    catch(...){result.hresult=static_cast<std::int32_t>(E_UNEXPECTED);}
    return result;
}
template<class T,class Query> void Observe(Observed<T>& result,Query&& query)noexcept {
    result.call=ObserveCall([&]{result.value=query();});
}
template<class T,class=void> struct HasIndirectProperty:std::false_type{};
template<class T> struct HasIndirectProperty<T,std::void_t<decltype(std::declval<T>().IsIndirectDisplayDevice())>>:std::true_type{};
template<class Adapter> void ObserveIndirect(const Adapter& adapter,Target& output)noexcept {
    output.indirectApiInBuild=HasIndirectProperty<Adapter>::value;
    if constexpr(HasIndirectProperty<Adapter>::value)Observe(output.indirect,[&]{return adapter.IsIndirectDisplayDevice();});
}
void ReadTarget(const DisplayTarget& target,Target& output)noexcept {
    Observe(output.id,[&]{return target.AdapterRelativeId();});
    Observe(output.connected,[&]{return target.IsConnected();});
    Observe(output.usage,[&]{return static_cast<std::int32_t>(target.UsageKind());});
    DisplayAdapter adapter{nullptr};
    output.adapter=ObserveCall([&]{adapter=target.Adapter();if(!adapter)winrt::throw_hresult(E_POINTER);});
    if(Known(output.adapter)){
        Observe(output.adapterId,[&]{const auto id=adapter.Id();return AdapterId{id.LowPart,id.HighPart};});
        ObserveIndirect(adapter,output);
    }
    Observe(output.stale,[&]{return target.IsStale();});
}
Snapshot ReadStatus(){
    Snapshot output;
    output.apartment=ObserveCall([]{winrt::init_apartment(winrt::apartment_type::multi_threaded);});
    if(!Known(output.apartment))return output;
    struct Apartment {~Apartment(){winrt::uninit_apartment();}} apartment;
    output.execution=ObserveCall([&]{
        // Create does not acquire targets. This diagnostic deliberately contains
        // no target acquisition, mode application, driver or session operation.
        DisplayManager manager{nullptr};
        output.manager=ObserveCall([&]{manager=DisplayManager::Create(DisplayManagerOptions::EnforceSourceOwnership);if(!manager)winrt::throw_hresult(E_POINTER);});
        if(!Known(output.manager))return;
        DisplayManagerResultWithState result{nullptr};DisplayState state{nullptr};
        output.stateRead=ObserveCall([&]{result=manager.TryReadCurrentStateForAllTargets();if(!result)winrt::throw_hresult(E_POINTER);});
        if(Known(output.stateRead)){
            Observe(output.stateResult,[&]{return static_cast<std::int32_t>(result.ErrorCode());});
            Observe(output.stateExtendedError,[&]{return static_cast<std::int32_t>(result.ExtendedErrorCode());});
            if(Known(output.stateResult)&&output.stateResult.value==0){
                output.stateObject=ObserveCall([&]{state=result.State();if(!state)winrt::throw_hresult(E_POINTER);});
                if(Known(output.stateObject))Observe(output.stateReadOnly,[&]{return state.IsReadOnly();});
            }
        }
        winrt::Windows::Foundation::Collections::IVectorView<DisplayTarget> targets{nullptr};
        output.enumeration=ObserveCall([&]{targets=manager.GetCurrentTargets();if(!targets)winrt::throw_hresult(E_POINTER);});
        if(Known(output.enumeration)){
            Observe(output.targetCount,[&]{return targets.Size();});
            if(Known(output.targetCount)){
                if(output.targetCount.value>MaximumTargets)winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
                output.targets.reserve(output.targetCount.value);
                for(std::uint32_t i=0;i<output.targetCount.value;++i){
                    Target observed;DisplayTarget target{nullptr};
                    observed.retrieval=ObserveCall([&]{target=targets.GetAt(i);if(!target)winrt::throw_hresult(E_POINTER);});
                    if(Known(observed.retrieval))ReadTarget(target,observed);
                    output.targets.push_back(observed);
                }
                output.enumerationComplete=true;
            }
        }
        // A change during any preceding observation invalidates eligibility.
        if(Known(output.stateObject))Observe(output.stateStale,[&]{return state.IsStale();});
    });
    return output;
}
}
int wmain(int argc,wchar_t* argv[]){
    using namespace retsar::lab::physical_display;
    const auto command=Parse(argc,argv);
    if(command==Command::Help){
        std::cout<<"RETSAR physical-display diagnostic\n"
            <<"Usage: RETSAR-physical-display-probe.exe --status\n"
            <<"       RETSAR-physical-display-probe.exe --help\n\n"
            <<"--status reads Display.Core targets and emits JSON; it never acquires or assigns one.\n"
            <<"Standard monitors are ineligible for this specialized-display route.\n"
            <<"A specialized candidate is not proof of ownership, presentation, session binding or independent input.\n"
            <<"Unavailable optional SDK observations remain unknown and cannot produce a candidate.\n"
            <<"No monitor names, serial numbers, device paths, EDID or screen pixels are collected.\n"
            <<"Exit 0: required observations complete and coherent; exit 2: incomplete or stale.\n"
            <<"Exit 64: invalid arguments. Help and invalid arguments initialize no Windows APIs.\n";
        return 0;
    }
    if(command==Command::Invalid){std::cerr<<"Use --status or --help as the sole argument. No display query was made.\n";return 64;}
    try{const auto result=ReadStatus();std::cout<<ToJson(result)<<'\n';return Complete(result)?0:2;}
    catch(...){std::cout<<"{\"schema\":\"retsar.physical-display-status.v1\",\"complete\":false,\"fatalSerializationFailure\":true,\"acquisitionAttempted\":false,\"displayAssignmentAttempted\":false,\"physicalSeatVerified\":false}\n";return 2;}
}
