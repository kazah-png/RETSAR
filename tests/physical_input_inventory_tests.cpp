#include "../src/desktop/physical_input_inventory.hpp"
#include <iostream>
#include <cstring>
using namespace retsar::desktop::input;
namespace {
GUID Id(unsigned value){return GUID{value,0,0,{0}};}
Observation Item(std::uintptr_t token,DWORD type,unsigned container,Bus bus=Bus::Usb){return {{token,type},{Id(container),true,bus,true,ERROR_SUCCESS}};}
struct FakeSource final:Source {
    std::vector<Observation> data;
    std::vector<Collection> finalList;
    unsigned enumerations=0,resolutions=0;
    bool failFirst=false,failFinal=false,useFinal=false;
    bool Enumerate(std::vector<Collection>& result,DWORD& error) override {
        ++enumerations;result.clear();
        if((enumerations==1&&failFirst)||(enumerations==2&&failFinal)){error=ERROR_ACCESS_DENIED;return false;}
        error=ERROR_SUCCESS;
        if(enumerations>1&&useFinal){result=finalList;return true;}
        for(const auto& entry:data)result.push_back(entry.collection);
        return true;
    }
    Metadata Resolve(const Collection& collection) override {
        ++resolutions;
        for(const auto& entry:data)if(entry.collection.token==collection.token)return entry.metadata;
        Metadata missing;missing.error=ERROR_NOT_FOUND;return missing;
    }
};
struct Node {
    DEVINST id=0,parent=0;
    GUID container=Id(1),bus{};
    DEVPROPTYPE type=DEVPROP_TYPE_GUID;
    ULONG bytes=sizeof(GUID);
    bool missingContainer=false,missingBus=false;
};
std::vector<Node> nodes;
unsigned parentCalls=0,busCalls=0;
CONFIGRET WINAPI NodeParent(PDEVINST parent,DEVINST node,ULONG flags){
    ++parentCalls;if(flags)return CR_INVALID_FLAG;
    for(const auto& entry:nodes)if(entry.id==node){if(!entry.parent)return CR_NO_SUCH_DEVNODE;*parent=entry.parent;return CR_SUCCESS;}
    return CR_NO_SUCH_DEVNODE;
}
CONFIGRET WINAPI NodeProperty(DEVINST node,const DEVPROPKEY* key,DEVPROPTYPE* type,PBYTE data,PULONG bytes,ULONG flags){
    if(flags)return CR_INVALID_FLAG;
    const bool container=key->pid==detail::ContainerKey.pid&&SameGuid(key->fmtid,detail::ContainerKey.fmtid);
    const bool bus=key->pid==detail::BusKey.pid&&SameGuid(key->fmtid,detail::BusKey.fmtid);
    if(!container&&!bus)return CR_NO_SUCH_VALUE;
    if(bus)++busCalls;
    for(const auto& entry:nodes)if(entry.id==node){
        if((container&&entry.missingContainer)||(bus&&entry.missingBus))return CR_NO_SUCH_VALUE;
        if(*bytes<sizeof(GUID))return CR_BUFFER_SMALL;
        const auto& value=container?entry.container:entry.bus;
        std::memcpy(data,&value,sizeof(value));*type=entry.type;*bytes=entry.bytes;return CR_SUCCESS;
    }
    return CR_NO_SUCH_DEVNODE;
}
}
int main(){
    unsigned passed=0,failed=0;
    const auto check=[&](bool value,const char* label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';value?++passed:++failed;};
    auto facts=SampleFacts();
    check(facts.complete,"memory-only fixture has complete metadata");
    check(facts.keyboardContainers==1&&facts.mouseContainers==1,"two keyboard collections count as one physical-bus container");
    check(facts.rawCollections==3&&facts.devices.size()==2,"collection count is retained separately from device count");
    check(!facts.additionalPairObserved&&!facts.assignmentSupported,"one keyboard and one mouse do not provide a spare pair");
    check(facts.devices[0].label==L"Keyboard 1"&&facts.devices[1].label==L"Mouse 1","UI labels are anonymous and readable");
    check(facts.devices[0].collectionCount==2&&facts.devices[0].connected,"group retains collection count and observed connection");
    const std::vector<Observation> twoPairs{Item(1,RIM_TYPEKEYBOARD,1),Item(2,RIM_TYPEMOUSE,2),Item(3,RIM_TYPEKEYBOARD,3),Item(4,RIM_TYPEMOUSE,4)};
    facts=Aggregate(twoPairs);
    check(facts.complete&&facts.additionalPairObserved,"two fully mapped USB pairs are hardware candidates");
    check(!facts.assignmentSupported,"candidate hardware never enables exclusive assignment");
    auto entries=twoPairs;entries.push_back(Item(5,RIM_TYPEKEYBOARD,1));entries.push_back(Item(6,RIM_TYPEMOUSE,2));facts=Aggregate(entries);
    check(facts.keyboardContainers==2&&facts.mouseContainers==2&&facts.devices.size()==4,"duplicate TLCs never inflate either kind");
    entries={Item(1,RIM_TYPEKEYBOARD,1),Item(2,RIM_TYPEMOUSE,1)};facts=Aggregate(entries);
    check(facts.keyboardContainers==1&&facts.mouseContainers==1,"a composite receiver groups once per kind");
    check(!facts.additionalPairObserved,"one composite container is never a spare pair");
    entries={Item(1,RIM_TYPEKEYBOARD,1),Item(2,RIM_TYPEKEYBOARD,1),Item(3,RIM_TYPEKEYBOARD,1)};facts=Aggregate(entries);
    check(facts.keyboardContainers==1&&facts.mouseContainers==0&&!facts.additionalPairObserved,"many keyboard TLCs cannot supply a mouse or spare pair");
    entries=twoPairs;entries[0].metadata.containerKnown=false;facts=Aggregate(entries);
    check(!facts.complete&&facts.unknownCollections==1&&!facts.additionalPairObserved,"missing container identity blocks spare-pair confidence");
    check(facts.keyboardContainers==1&&facts.mouseContainers==2,"known partial counts survive a missing identity");
    check(!facts.devices[0].containerResolved,"anonymous unresolved collection does not claim a resolved container");
    entries=twoPairs;entries[0].metadata.container=GUID{};facts=Aggregate(entries);
    check(!facts.complete&&facts.unknownCollections==1,"zero container GUID is not a physical identity");
    entries=twoPairs;entries[0].metadata.container=SystemContainer;facts=Aggregate(entries);
    check(!facts.complete&&facts.keyboardContainers==1,"machine-wide system container cannot stand in for a peripheral");
    entries=twoPairs;entries[0].metadata.bus=Bus::Unknown;facts=Aggregate(entries);
    check(!facts.complete&&facts.unknownCollections==1&&!facts.additionalPairObserved,"unknown bus never claims physical or virtual hardware");
    entries={Item(1,RIM_TYPEKEYBOARD,1,Bus::Software),Item(2,RIM_TYPEMOUSE,2,Bus::Software)};facts=Aggregate(entries);
    check(facts.complete&&facts.softwareContainers==2&&facts.unknownCollections==0,"explicit software-bus evidence is separately reported");
    check(facts.keyboardContainers==0&&facts.mouseContainers==0&&!facts.additionalPairObserved,"software-bus devices never count toward physical candidates");
    entries={Item(1,RIM_TYPEKEYBOARD,1),Item(2,RIM_TYPEKEYBOARD,1,Bus::Unknown)};facts=Aggregate(entries);
    check(!facts.complete&&facts.unknownCollections==2&&facts.keyboardContainers==0,"unknown TLC bus invalidates its whole shared-container group");
    check(facts.devices[0].bus==Bus::Unknown,"conflicting bus evidence is not reduced to a favorable classification");
    entries={Item(1,RIM_TYPEKEYBOARD,1),Item(2,RIM_TYPEKEYBOARD,1,Bus::Software)};facts=Aggregate(entries);
    check(!facts.complete&&facts.keyboardContainers==0&&facts.softwareContainers==0,"conflicting physical and software metadata remains unknown");
    entries=twoPairs;entries[0].metadata.connected=false;facts=Aggregate(entries);
    check(!facts.complete&&facts.keyboardContainers==1&&!facts.devices[0].connected,"disconnected interface is not counted as usable hardware");
    entries=twoPairs;entries[0].metadata.error=ERROR_ACCESS_DENIED;facts=Aggregate(entries);
    check(!facts.complete&&facts.error==ERROR_ACCESS_DENIED&&facts.unknownCollections==1,"resolution error survives without accepting partial identity");
    facts=Aggregate(twoPairs,false,ERROR_RETRY);
    check(!facts.complete&&facts.changedDuringRead&&!facts.additionalPairObserved,"enumeration churn invalidates spare-pair conclusion");
    check(std::all_of(facts.devices.begin(),facts.devices.end(),[](const Device& d){return !d.connected;}),"stale snapshot does not claim current connection");
    facts=Aggregate({},true);check(facts.complete&&facts.devices.empty()&&!facts.additionalPairObserved,"complete empty local observation never asserts a pair");
    check(ClassifyBus(UsbBus)==Bus::Usb&&ClassifyBus(SoftwareBus)==Bus::Software,"only explicit known bus GUIDs classify");
    check(ClassifyBus(Id(42))==Bus::Unknown&&ClassifyBus(GUID{})==Bus::Unknown,"unfamiliar and missing bus GUIDs remain unknown");
    check(!UsableContainer(GUID{})&&!UsableContainer(SystemContainer)&&UsableContainer(Id(1)),"container validity excludes zero and host-wide identities");
    check(std::wstring(BusLabel(Bus::Unknown))==L"Bus not classified","unknown bus wording does not assert virtual hardware");
    FakeSource source;source.data=twoPairs;facts=Read(source);
    check(source.enumerations==2&&source.resolutions==4,"read observes metadata then rechecks list without subscribing to events");
    check(facts.complete&&facts.additionalPairObserved,"injected stable source yields expected candidate counts");
    source=FakeSource{};source.data=twoPairs;source.failFirst=true;facts=Read(source);
    check(!facts.complete&&facts.error==ERROR_ACCESS_DENIED&&source.resolutions==0,"initial list failure stops metadata reads");
    source=FakeSource{};source.data=twoPairs;source.failFinal=true;facts=Read(source);
    check(!facts.complete&&facts.changedDuringRead&&facts.error==ERROR_ACCESS_DENIED,"failed final enumeration cannot certify stale hardware");
    source=FakeSource{};source.data=twoPairs;source.useFinal=true;source.finalList={{4,RIM_TYPEMOUSE},{3,RIM_TYPEKEYBOARD},{2,RIM_TYPEMOUSE},{1,RIM_TYPEKEYBOARD}};facts=Read(source);
    check(facts.complete&&!facts.changedDuringRead,"same handles in different enumeration order are stable");
    source.finalList.push_back({5,RIM_TYPEKEYBOARD});source.enumerations=source.resolutions=0;facts=Read(source);
    check(!facts.complete&&facts.changedDuringRead&&facts.error==ERROR_RETRY,"new collection during read is detected");
    source.finalList={{1,RIM_TYPEKEYBOARD},{2,RIM_TYPEMOUSE},{3,RIM_TYPEKEYBOARD}};source.enumerations=source.resolutions=0;facts=Read(source);
    check(!facts.complete&&facts.changedDuringRead,"removed collection during read is detected");
    source.finalList={{1,RIM_TYPEMOUSE},{2,RIM_TYPEMOUSE},{3,RIM_TYPEKEYBOARD},{4,RIM_TYPEMOUSE}};source.enumerations=source.resolutions=0;facts=Read(source);
    check(!facts.complete&&facts.changedDuringRead,"same raw handle with changed kind is not stable");
    source=FakeSource{};source.data=twoPairs;source.data.push_back(Item(9,RIM_TYPEHID,9));facts=Read(source);
    check(facts.complete&&facts.rawCollections==4&&source.resolutions==4,"other HID metadata is ignored without resolution");
    source=FakeSource{};source.data=twoPairs;source.data.push_back(twoPairs[0]);facts=Read(source);
    check(!facts.complete&&facts.error==ERROR_INVALID_DATA&&source.resolutions==0,"duplicate raw handle invalidates the list rather than inflating counts");
    source=FakeSource{};source.data={Item(0,RIM_TYPEMOUSE,1)};facts=Read(source);
    check(!facts.complete&&facts.error==ERROR_INVALID_DATA&&source.resolutions==0,"null raw identity is never probed");
    source=FakeSource{};source.data=twoPairs;source.useFinal=true;source.finalList={{1,RIM_TYPEKEYBOARD},{1,RIM_TYPEKEYBOARD}};facts=Read(source);
    check(!facts.complete&&facts.error==ERROR_INVALID_DATA&&!facts.additionalPairObserved,"invalid final snapshot is conservative");
    source=FakeSource{};source.data.resize(MaxCollections+1);facts=Read(source);
    check(!facts.complete&&facts.error==ERROR_BUFFER_OVERFLOW&&source.resolutions==0,"oversized initial inventory is bounded before resolution");
    entries.resize(MaxCollections+1);facts=Aggregate(entries);
    check(!facts.complete&&facts.error==ERROR_BUFFER_OVERFLOW&&facts.devices.empty(),"pure aggregation also rejects oversized inventory");
    source=FakeSource{};source.data=twoPairs;source.data[0].metadata.error=ERROR_NOT_FOUND;facts=Read(source);
    check(!facts.complete&&facts.error==ERROR_NOT_FOUND&&source.enumerations==2,"disappearing metadata triggers incomplete results even if raw list is unchanged");
    source=FakeSource{};source.data={Item(1,RIM_TYPEKEYBOARD,0),Item(2,RIM_TYPEKEYBOARD,0)};facts=Read(source);
    check(facts.unknownCollections==2&&facts.devices.size()==2&&facts.keyboardContainers==0,"anonymous unresolved identities are not merged into a fictitious device");
    const detail::NodeApi nodeApi{NodeProperty,NodeParent};
    nodes={{1,0,Id(1),UsbBus}};parentCalls=busCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Usb&&parentCalls==0,"direct USB evidence needs no parent traversal");
    nodes={{1,2,Id(1),Id(77)},{2,0,Id(1),UsbBus}};parentCalls=busCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Usb&&parentCalls==1,"HID child can inherit USB evidence only within its own container");
    nodes[1].container=Id(2);parentCalls=busCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown&&busCalls==1,"parent outside container cannot confer USB identity");
    nodes[1].container=Id(1);nodes[1].missingContainer=true;parentCalls=busCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown&&busCalls==1,"missing parent container stops the walk before classifying its bus");
    nodes={{1,2,Id(1),SoftwareBus},{2,0,Id(1),UsbBus}};parentCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Software&&parentCalls==0,"explicit software bus is not overridden by a USB ancestor");
    nodes={{1,2,Id(1),Id(77)},{2,1,Id(1),Id(77)}};parentCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown&&parentCalls==2,"ancestry cycle terminates without a physical claim");
    nodes.clear();for(DEVINST i=1;i<=9;++i)nodes.push_back({i,i+1,Id(1),i==9?UsbBus:Id(77)});parentCalls=busCalls=0;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown&&busCalls==8&&parentCalls==8,"ancestry is bounded even if a later node reports USB");
    nodes={{1,0,Id(1),UsbBus}};nodes[0].type=DEVPROP_TYPE_STRING;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown,"GUID bytes with wrong property type are rejected");
    nodes[0].type=DEVPROP_TYPE_GUID;nodes[0].bytes=sizeof(GUID)-1;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown,"truncated bus property is rejected");
    nodes[0].bytes=sizeof(GUID)+1;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown,"oversized bus property is rejected");
    nodes[0].bytes=sizeof(GUID);nodes[0].missingBus=true;
    check(detail::BusEvidence(1,Id(1),nodeApi)==Bus::Unknown,"failed bus query never defaults to physical");
    std::cout<<"Physical input inventory: "<<passed<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
