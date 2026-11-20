#pragma once
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

// Only device metadata is read. No Raw Input registration, event stream, device
// endpoint, hook, ownership change, or driver configuration belongs in this file.
namespace retsar::desktop::input {
enum class Kind { Keyboard, Mouse };
enum class Bus { Unknown, Usb, Software };
struct Device {
    Kind kind=Kind::Keyboard;
    std::wstring label;
    unsigned collectionCount=0;
    Bus bus=Bus::Unknown;
    bool connected=false;
    bool containerResolved=false;
};
struct Facts {
    bool complete=false;
    bool changedDuringRead=false;
    bool additionalPairObserved=false;
    // A hardware candidate is not permission or support for exclusive routing.
    bool assignmentSupported=false;
    unsigned keyboardContainers=0;
    unsigned mouseContainers=0;
    unsigned unknownCollections=0;
    unsigned softwareContainers=0;
    unsigned rawCollections=0;
    DWORD error=ERROR_SUCCESS;
    std::vector<Device> devices; // Anonymous; contains no paths, handles or GUIDs.
};
inline constexpr unsigned MaxCollections=4096;
inline constexpr GUID UsbBus{0x9d7debbc,0xc85d,0x11d1,{0x9e,0xb4,0x00,0x60,0x08,0xc3,0xa1,0x9a}};
inline constexpr GUID SoftwareBus{0x06d10322,0x7de0,0x4cef,{0x8e,0x25,0x19,0x7d,0x0e,0x74,0x42,0xe2}};
inline constexpr GUID SystemContainer{0,0,0,{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}};
inline bool SameGuid(const GUID& a,const GUID& b) noexcept { return InlineIsEqualGUID(a,b)!=FALSE; }
inline bool UsableContainer(const GUID& value) noexcept {
    const GUID zero{};return !SameGuid(value,zero)&&!SameGuid(value,SystemContainer);
}
inline Bus ClassifyBus(const GUID& value) noexcept {
    if(SameGuid(value,UsbBus))return Bus::Usb;
    if(SameGuid(value,SoftwareBus))return Bus::Software;
    // HID, internal, ACPI and unfamiliar buses do not prove physical hardware.
    return Bus::Unknown;
}
inline const wchar_t* BusLabel(Bus bus) noexcept {
    switch(bus){case Bus::Usb:return L"USB bus reported";case Bus::Software:return L"Software bus reported";default:return L"Bus not classified";}
}
struct Collection { std::uintptr_t token=0;DWORD type=RIM_TYPEHID; };
struct Metadata {
    GUID container{};
    bool containerKnown=false;
    Bus bus=Bus::Unknown;
    bool connected=false;
    DWORD error=ERROR_SUCCESS;
};
// Injectable metadata boundary; implementations must not capture input events.
// Instances are owned by a single read. The public Read() has no shared state.
struct Source {
    virtual ~Source()=default;
    virtual bool Enumerate(std::vector<Collection>& result,DWORD& error)=0;
    virtual Metadata Resolve(const Collection& collection)=0;
};
inline bool Relevant(const Collection& value) noexcept { return value.type==RIM_TYPEKEYBOARD||value.type==RIM_TYPEMOUSE; }
inline Kind CollectionKind(const Collection& value) noexcept { return value.type==RIM_TYPEKEYBOARD?Kind::Keyboard:Kind::Mouse; }
struct Observation { Collection collection;Metadata metadata; };

inline Facts Aggregate(const std::vector<Observation>& observations,bool stable=true,DWORD observationError=ERROR_SUCCESS){
    struct Group { GUID id{};Kind kind=Kind::Keyboard;Bus bus=Bus::Unknown;unsigned count=0;bool connected=true;bool known=false;bool valid=true; };
    Facts facts;facts.complete=stable&&observationError==ERROR_SUCCESS;facts.changedDuringRead=!stable;facts.error=observationError;
    if(observations.size()>MaxCollections){facts.complete=false;facts.error=ERROR_BUFFER_OVERFLOW;return facts;}
    std::vector<Group> groups;
    for(const auto& observation:observations){
        if(!Relevant(observation.collection))continue;
        ++facts.rawCollections;
        const auto& metadata=observation.metadata;
        const auto kind=CollectionKind(observation.collection);
        const bool known=metadata.containerKnown&&UsableContainer(metadata.container);
        auto found=groups.end();
        if(known)found=std::find_if(groups.begin(),groups.end(),[&](const Group& group){return group.known&&group.kind==kind&&SameGuid(group.id,metadata.container);});
        const bool valid=known&&metadata.connected&&metadata.error==ERROR_SUCCESS&&metadata.bus!=Bus::Unknown;
        if(found==groups.end())groups.push_back({metadata.container,kind,metadata.bus,1,metadata.connected,known,valid});
        else {
            ++found->count;found->connected=found->connected&&metadata.connected;found->valid=found->valid&&valid;
            if(found->bus!=metadata.bus){found->bus=Bus::Unknown;found->valid=false;}
        }
        if(metadata.error!=ERROR_SUCCESS&&facts.error==ERROR_SUCCESS)facts.error=metadata.error;
    }
    unsigned keyboards=0,mice=0;
    for(const auto& group:groups){
        const auto index=group.kind==Kind::Keyboard?++keyboards:++mice;
        Device device;device.kind=group.kind;device.label=(group.kind==Kind::Keyboard?L"Keyboard ":L"Mouse ")+std::to_wstring(index);
        device.collectionCount=group.count;device.bus=group.bus;device.connected=stable&&group.connected;device.containerResolved=group.known;
        facts.devices.push_back(std::move(device));
        if(!group.valid){facts.complete=false;facts.unknownCollections+=group.count;}
        else if(group.bus==Bus::Usb){if(group.kind==Kind::Keyboard)++facts.keyboardContainers;else ++facts.mouseContainers;}
        else if(group.bus==Bus::Software)++facts.softwareContainers;
    }
    facts.additionalPairObserved=facts.complete&&facts.keyboardContainers>=2&&facts.mouseContainers>=2;
    return facts;
}
inline bool Normalize(std::vector<Collection>& values,DWORD& error){
    if(values.size()>MaxCollections){error=ERROR_BUFFER_OVERFLOW;return false;}
    values.erase(std::remove_if(values.begin(),values.end(),[](const Collection& c){return !Relevant(c);}),values.end());
    std::sort(values.begin(),values.end(),[](const Collection& a,const Collection& b){return a.token<b.token||(a.token==b.token&&a.type<b.type);});
    for(size_t i=0;i<values.size();++i){
        if(values[i].token==0||(i&&values[i].token==values[i-1].token)){error=ERROR_INVALID_DATA;return false;}
    }
    return true;
}
inline Facts Read(Source& source){
    try {
        std::vector<Collection> before,after;DWORD error=ERROR_SUCCESS;
        if(!source.Enumerate(before,error)||!Normalize(before,error)){
            Facts facts;facts.error=error==ERROR_SUCCESS?ERROR_GEN_FAILURE:error;return facts;
        }
        std::vector<Observation> observations;observations.reserve(before.size());
        for(const auto& collection:before)observations.push_back({collection,source.Resolve(collection)});
        if(!source.Enumerate(after,error)||!Normalize(after,error))return Aggregate(observations,false,error==ERROR_SUCCESS?ERROR_GEN_FAILURE:error);
        const bool stable=before.size()==after.size()&&std::equal(before.begin(),before.end(),after.begin(),[](const Collection& a,const Collection& b){return a.token==b.token&&a.type==b.type;});
        return Aggregate(observations,stable,stable?ERROR_SUCCESS:ERROR_RETRY);
    } catch(const std::bad_alloc&){Facts facts;facts.error=ERROR_NOT_ENOUGH_MEMORY;return facts;}
}

namespace detail {
// SDK DEVPKEY_Device_ContainerId and DEVPKEY_Device_BusTypeGuid, represented
// locally to avoid global INITGUID definitions in a header included by the UI.
inline constexpr DEVPROPKEY ContainerKey{{0x8c7ed206,0x3f8a,0x4827,{0xb3,0xab,0xae,0x9e,0x1f,0xae,0xfc,0x6c}},2};
inline constexpr DEVPROPKEY BusKey{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},21};
struct NodeApi { decltype(&CM_Get_DevNode_PropertyW) property=&CM_Get_DevNode_PropertyW;decltype(&CM_Get_Parent) parent=&CM_Get_Parent; };
inline bool GuidProperty(const NodeApi& api,DEVINST node,const DEVPROPKEY& key,GUID& result){
    DEVPROPTYPE type=DEVPROP_TYPE_EMPTY;ULONG bytes=sizeof(result);
    return api.property(node,&key,&type,reinterpret_cast<PBYTE>(&result),&bytes,0)==CR_SUCCESS&&type==DEVPROP_TYPE_GUID&&bytes==sizeof(result);
}
inline Bus BusEvidence(DEVINST node,const GUID& container,const NodeApi& api=NodeApi{}){
    // A HID child often reports only the HID bus. Follow at most eight parent
    // nodes, and only inside the SAME container: a USB hub elsewhere must not
    // turn an unrelated/virtual collection into a physical device candidate.
    DEVINST visited[8]{};
    for(unsigned depth=0;depth<8;++depth){
        for(unsigned prior=0;prior<depth;++prior)if(visited[prior]==node)return Bus::Unknown;
        visited[depth]=node;
        GUID bus{};
        if(GuidProperty(api,node,BusKey,bus)){const auto classified=ClassifyBus(bus);if(classified!=Bus::Unknown)return classified;}
        DEVINST parent=0;GUID parentContainer{};
        if(api.parent(&parent,node,0)!=CR_SUCCESS||!GuidProperty(api,parent,ContainerKey,parentContainer)||!SameGuid(parentContainer,container))return Bus::Unknown;
        node=parent;
    }
    return Bus::Unknown;
}
struct DeviceSet {
    HDEVINFO value=INVALID_HANDLE_VALUE;
    ~DeviceSet(){if(value!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(value);}
    DeviceSet()=default;DeviceSet(const DeviceSet&)=delete;DeviceSet& operator=(const DeviceSet&)=delete;
};
class WindowsSource final:public Source {
public:
    bool Enumerate(std::vector<Collection>& result,DWORD& error) override {
        result.clear();error=ERROR_SUCCESS;
        for(unsigned attempt=0;attempt<3;++attempt){
            UINT count=0;
            if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==static_cast<UINT>(-1)){error=GetLastError();return false;}
            if(count>MaxCollections){error=ERROR_BUFFER_OVERFLOW;return false;}
            if(!count)return true;
            std::vector<RAWINPUTDEVICELIST> raw(count);UINT capacity=count;
            const UINT received=GetRawInputDeviceList(raw.data(),&capacity,sizeof(RAWINPUTDEVICELIST));
            if(received==static_cast<UINT>(-1)){error=GetLastError();if(error==ERROR_INSUFFICIENT_BUFFER)continue;return false;}
            if(received>count){error=ERROR_INVALID_DATA;return false;}
            result.reserve(received);
            for(UINT i=0;i<received;++i)if(raw[i].dwType==RIM_TYPEKEYBOARD||raw[i].dwType==RIM_TYPEMOUSE)result.push_back({reinterpret_cast<std::uintptr_t>(raw[i].hDevice),raw[i].dwType});
            error=ERROR_SUCCESS;return true;
        }
        error=ERROR_RETRY;return false;
    }
    Metadata Resolve(const Collection& collection) override {
        Metadata result;
        if(!Relevant(collection)||!collection.token){result.error=ERROR_INVALID_PARAMETER;return result;}
        UINT chars=0;const auto handle=reinterpret_cast<HANDLE>(collection.token);
        if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&chars)==static_cast<UINT>(-1)){result.error=GetLastError();return result;}
        if(chars==0||chars>32767){result.error=ERROR_INVALID_DATA;return result;}
        // Sentinel storage plus explicit terminator validation; never parse IDs.
        std::vector<wchar_t> path(static_cast<size_t>(chars)+1,L'\xffff');UINT capacity=chars;
        const UINT received=GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,path.data(),&capacity);
        if(received==static_cast<UINT>(-1)){result.error=GetLastError();return result;}
        if(capacity>chars||std::find(path.begin(),path.begin()+chars,L'\0')==path.begin()+chars||path.front()==L'\0'){
            result.error=ERROR_INVALID_DATA;return result;
        }
        DeviceSet set;set.value=SetupDiCreateDeviceInfoList(nullptr,nullptr);
        if(set.value==INVALID_HANDLE_VALUE){result.error=GetLastError();return result;}
        SP_DEVICE_INTERFACE_DATA interfaceData{};interfaceData.cbSize=sizeof(interfaceData);
        if(!SetupDiOpenDeviceInterfaceW(set.value,path.data(),0,&interfaceData)){result.error=GetLastError();return result;}
        result.connected=(interfaceData.Flags&SPINT_ACTIVE)!=0&&(interfaceData.Flags&SPINT_REMOVED)==0;
        SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);DWORD required=0;
        // Documented metadata-only use: NULL details returns INSUFFICIENT_BUFFER
        // while still filling the associated SP_DEVINFO_DATA. No HID open.
        const BOOL detailOk=SetupDiGetDeviceInterfaceDetailW(set.value,&interfaceData,nullptr,0,&required,&device);
        const DWORD detailError=detailOk?ERROR_SUCCESS:GetLastError();
        if((!detailOk&&detailError!=ERROR_INSUFFICIENT_BUFFER)||device.DevInst==0){result.error=detailError==ERROR_SUCCESS?ERROR_INVALID_DATA:detailError;return result;}
        DEVPROPTYPE propertyType=DEVPROP_TYPE_EMPTY;DWORD bytes=0;
        if(!SetupDiGetDevicePropertyW(set.value,&device,&ContainerKey,&propertyType,reinterpret_cast<PBYTE>(&result.container),sizeof(result.container),&bytes,0)){
            result.error=GetLastError();return result;
        }
        result.containerKnown=propertyType==DEVPROP_TYPE_GUID&&bytes==sizeof(result.container)&&UsableContainer(result.container);
        if(!result.containerKnown){result.error=ERROR_INVALID_DATA;return result;}
        result.bus=BusEvidence(device.DevInst,result.container);
        return result;
    }
};
} // namespace detail

inline Facts Read(){detail::WindowsSource source;return Read(source);}
inline Facts SampleFacts(){
    // Entirely in memory; one keyboard exposing two collections and one mouse.
    const GUID keyboard{1,0,0,{0}},mouse{2,0,0,{0}};
    return Aggregate({{{1,RIM_TYPEKEYBOARD},{keyboard,true,Bus::Usb,true,ERROR_SUCCESS}},
                      {{2,RIM_TYPEKEYBOARD},{keyboard,true,Bus::Usb,true,ERROR_SUCCESS}},
                      {{3,RIM_TYPEMOUSE},{mouse,true,Bus::Usb,true,ERROR_SUCCESS}}});
}
} // namespace retsar::desktop::input
