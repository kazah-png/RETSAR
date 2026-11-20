#include "../src/lab/child_session_resolution.hpp"
#include <cwchar>
#include <iostream>
using namespace seats::lab::child;
namespace {
namespace resolution = seats::lab::child::resolution;
unsigned passed = 0, failed = 0;
void Check(bool value, const char* name) { if (value) ++passed; else { ++failed; std::cerr << "FAIL " << name << '\n'; } }
const auto DefaultSettings = resolution::ForViewport(1920, 1080);
enum class Stage { None, Add, Query, Names, Connected, Update, Release, Width, Height, Deferred };
class Client final : public IDispatch {
public:
    unsigned& depth;
    ULONG refs = 1;
    unsigned adds = 0, releases = 0, queries = 0, calls = 0, reads = 0;
    bool guarded = true, shape = true, nullVersion = false;
    HRESULT versionResult = S_OK, namesResult = S_OK, updateResult = S_OK, readResult = S_OK;
    VARTYPE connectedType = VT_I2, propertyType = VT_I4;
    LONG connected = 1, width = 1920, height = 1080;
    Stage reenter = Stage::None, cancelAt = Stage::None;
    HRESULT reentry = S_OK;
    bool* cancelled = nullptr;
    resolution::Settings expected = DefaultSettings;
    resolution::Pending* synchronous = nullptr;
    explicit Client(unsigned& value) : depth(value) {}
    void At(Stage stage) {
        guarded = guarded && depth == 1;
        if (cancelled && cancelAt == stage) *cancelled = true;
        if (reenter == stage) reentry = resolution::Request(this, DefaultSettings, depth).result;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        At(Stage::Query); ++queries;
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(iid, resolution::Client9Id)) { shape = false; return E_NOINTERFACE; }
        if (versionResult != S_OK) return versionResult;
        if (!nullVersion) { *out = static_cast<IUnknown*>(this); AddRef(); }
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { ++adds; At(Stage::Add); return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ++releases; At(Stage::Release); return --refs; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out) override { if (out) *out = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count, LCID, DISPID* out) override {
        guarded = guarded && depth == 1;
        if (!IsEqualIID(iid, IID_NULL) || !names || count != 1 || !out) { shape = false; return E_INVALIDARG; }
        if (!std::wcscmp(names[0], L"Connected")) *out = 1;
        else if (!std::wcscmp(names[0], L"UpdateSessionDisplaySettings")) { At(Stage::Names); if (namesResult != S_OK) return namesResult; *out = 2; }
        else if (!std::wcscmp(names[0], L"DesktopWidth")) *out = 3;
        else if (!std::wcscmp(names[0], L"DesktopHeight")) *out = 4;
        else { shape = false; return DISP_E_UNKNOWNNAME; }
        return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE Fill(EXCEPINFO* exception) {
        auto* client = static_cast<Client*>(exception->pvReserved); client->At(Stage::Deferred);
        exception->scode = E_ACCESSDENIED; exception->wCode = 9;
        exception->bstrSource = SysAllocString(L"Synthetic");
        exception->bstrDescription = SysAllocString(L"Synthetic method failure");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags, DISPPARAMS* args,
        VARIANT* output, EXCEPINFO* exception, UINT*) override {
        guarded = guarded && depth == 1;
        shape = shape && IsEqualIID(iid, IID_NULL) && args && !args->cNamedArgs && !args->rgdispidNamedArgs;
        if (id == 2 && flags == DISPATCH_METHOD) {
            ++calls; At(Stage::Update);
            shape = shape && args && args->cArgs == 7 && args->rgvarg && !output;
            const ULONG expectedArguments[7]{expected.deviceScale, expected.desktopScale, expected.orientation,
                expected.physicalHeight, expected.physicalWidth, expected.height, expected.width};
            if (args && args->cArgs == 7 && args->rgvarg)
                for (unsigned i = 0; i != 7; ++i)
                    shape = shape && args->rgvarg[i].vt == VT_UI4 && args->rgvarg[i].ulVal == expectedArguments[i];
            if (synchronous) {
                VARIANT event[2]{}; event[0].vt = event[1].vt = VT_I4;
                event[0].lVal = static_cast<LONG>(expected.height); event[1].lVal = static_cast<LONG>(expected.width);
                DISPPARAMS parameters{event, nullptr, 2, 0}; synchronous->ObserveEvent(&parameters, 101);
            }
            if (updateResult == DISP_E_EXCEPTION && exception) {
                exception->pvReserved = this; exception->pfnDeferredFillIn = Fill;
            }
            return updateResult;
        }
        if (flags != DISPATCH_PROPERTYGET || id < 1 || id > 4 || id == 2 || !output) {
            shape = false; return DISP_E_MEMBERNOTFOUND;
        }
        ++reads; shape = shape && args && args->cArgs == 0;
        At(id == 1 ? Stage::Connected : id == 3 ? Stage::Width : Stage::Height);
        if (readResult != S_OK) return readResult;
        VariantInit(output); output->vt = id == 1 ? connectedType : propertyType;
        const LONG value = id == 1 ? connected : id == 3 ? width : height;
        if (output->vt == VT_I2) output->iVal = static_cast<SHORT>(value);
        else if (output->vt == VT_I4) output->lVal = value;
        else if (output->vt == VT_DISPATCH) { output->pdispVal = this; AddRef(); }
        return S_OK;
    }
};
struct Event {
    VARIANT arguments[2]{};
    DISPPARAMS parameters{arguments, nullptr, 2, 0};
    Event(LONG width, LONG height) {
        arguments[0].vt = arguments[1].vt = VT_I4;
        arguments[0].lVal = height; arguments[1].lVal = width;
    }
};
void SettingsTests() {
    Check(resolution::Valid(DefaultSettings), "normal 1080p request valid");
    const auto odd = resolution::ForViewport(1919, 1079);
    Check(odd.width == 1918 && odd.height == 1079 && resolution::Valid(odd), "odd viewport width rounds down only");
    Check(!resolution::Valid(resolution::ForViewport(-1, 1080)), "negative viewport rejected");
    Check(!resolution::Valid(resolution::ForViewport(1920, 0)), "zero height rejected");
    for (const ULONG width : {0ul, 199ul, 201ul, 8193ul, static_cast<ULONG>(MAXDWORD)}) {
        auto value = DefaultSettings; value.width = width; Check(!resolution::Valid(value), "invalid protocol width rejected");
    }
    for (const ULONG height : {0ul, 199ul, 8193ul, static_cast<ULONG>(MAXDWORD)}) {
        auto value = DefaultSettings; value.height = height; Check(!resolution::Valid(value), "invalid protocol height rejected");
    }
    auto value = resolution::ForViewport(8000, 2000);
    Check(resolution::Valid(value), "16M pixel boundary accepted");
    value.height++; Check(!resolution::Valid(value), "pixel budget evaluated without overflow");
    value = DefaultSettings; value.physicalWidth = 100;
    Check(!resolution::Valid(value), "partial unknown physical size rejected");
    value.physicalHeight = 10000; Check(resolution::Valid(value), "documented physical bounds accepted");
    value.physicalHeight++; Check(!resolution::Valid(value), "oversized physical size rejected");
    for (const ULONG orientation : {0ul, 90ul, 180ul, 270ul}) {
        value = DefaultSettings; value.orientation = orientation; Check(resolution::Valid(value), "documented orientation accepted");
    }
    value.orientation = 45; Check(!resolution::Valid(value), "arbitrary orientation rejected");
    for (const ULONG scale : {100ul, 140ul, 180ul}) {
        value = DefaultSettings; value.deviceScale = scale; Check(resolution::Valid(value), "documented device scale accepted");
    }
    value.deviceScale = 0; Check(!resolution::Valid(value), "zero device scale is not treated as preserve");
    value = DefaultSettings; value.desktopScale = 500; Check(resolution::Valid(value), "desktop scale upper bound accepted");
    value.desktopScale++; Check(!resolution::Valid(value), "desktop scale above bounds rejected");
    value.desktopScale = 99; Check(!resolution::Valid(value), "desktop scale below bounds rejected");
}
void DispatchTests() {
    unsigned depth = 0; Client client(depth);
    Check(resolution::Request(nullptr, DefaultSettings, depth).result == E_POINTER && !depth, "null client performs no COM call");
    Check(resolution::Request(&client, {}, depth).result == E_INVALIDARG && !client.adds, "invalid settings rejected before AddRef");
    depth = 1;
    Check(resolution::Request(&client, DefaultSettings, depth).result == E_PENDING && !client.adds && depth == 1, "busy context performs no COM call");
    depth = 0;
    auto result = resolution::Request(&client, DefaultSettings, depth);
    Check(result.result == S_OK && result.supported && result.attempted && result.accepted, "S_OK means request accepted only");
    Check(client.calls == 1 && client.queries == 1 && client.reads == 1 && client.shape, "only checked interface connected read and resize method used");
    Check(client.guarded && client.refs == 1 && client.adds == client.releases && !depth, "entire COM lifetime guarded and balanced");
    {
        Client fake(depth); fake.expected = {2468, 1357, 531, 298, 90, 125, 140};
        result = resolution::Request(&fake, fake.expected, depth);
        Check(result.accepted && fake.shape, "seven distinct ULONG values reverse correctly without named arguments");
    }
    for (const HRESULT failure : {E_NOINTERFACE, E_ACCESSDENIED, S_FALSE}) {
        Client fake(depth); fake.versionResult = failure; result = resolution::Request(&fake, DefaultSettings, depth);
        Check(!result.accepted && !result.attempted && !fake.reads && !fake.calls, "failed or noncanonical QI does not invoke method");
        Check(fake.guarded && fake.refs == 1 && !depth, "QI failure releases held client under guard");
    }
    {
        Client fake(depth); fake.nullVersion = true;
        Check(resolution::Request(&fake, DefaultSettings, depth).result == E_NOINTERFACE && !fake.calls, "successful QI with null pointer is unsupported");
    }
    for (const LONG state : {0l, 2l, -1l}) {
        Client fake(depth); fake.connected = state; result = resolution::Request(&fake, DefaultSettings, depth);
        Check(!result.attempted && !fake.calls, "disconnected connecting or unknown state cannot resize");
    }
    {
        Client fake(depth); fake.connectedType = VT_DISPATCH; result = resolution::Request(&fake, DefaultSettings, depth);
        Check(result.result == DISP_E_TYPEMISMATCH && !fake.calls && fake.guarded && fake.refs == 1, "invalid property variant is cleared before guard release");
    }
    {
        Client fake(depth); fake.readResult = E_FAIL;
        Check(resolution::Request(&fake, DefaultSettings, depth).result == E_FAIL && !fake.calls, "connected query failure preserved");
    }
    for (const HRESULT missing : {DISP_E_UNKNOWNNAME, DISP_E_MEMBERNOTFOUND}) {
        Client fake(depth); fake.namesResult = missing; result = resolution::Request(&fake, DefaultSettings, depth);
        Check(result.result == missing && !result.supported && !fake.calls, "missing method is honestly reported");
    }
    for (const HRESULT failure : {E_ACCESSDENIED, S_FALSE}) {
        Client fake(depth); fake.updateResult = failure; result = resolution::Request(&fake, DefaultSettings, depth);
        Check(result.result == failure && result.attempted && !result.accepted, "noncanonical method success never confirms resize");
    }
    {
        Client fake(depth); fake.updateResult = DISP_E_EXCEPTION; fake.reenter = Stage::Deferred;
        result = resolution::Request(&fake, DefaultSettings, depth);
        Check(result.result == DISP_E_EXCEPTION && ax::lastDispatch.deferred &&
            ax::lastDispatch.exceptionCode == E_ACCESSDENIED && ax::lastDispatch.exceptionWord == 9,
            "deferred exception diagnostics preserved and strings cleaned");
        Check(fake.reentry == E_PENDING && fake.guarded && fake.refs == 1 && !depth,
            "deferred exception fill remains guarded against reentry");
    }
    for (const auto stage : {Stage::Add, Stage::Query, Stage::Names, Stage::Connected, Stage::Update, Stage::Release}) {
        Client fake(depth); fake.reenter = stage; result = resolution::Request(&fake, DefaultSettings, depth);
        Check(result.accepted && fake.reentry == E_PENDING && fake.calls == 1, "reentry never recursively resizes");
        Check(fake.refs == 1 && fake.guarded && !depth, "reentry balances full reference lifetime");
    }
    for (const auto stage : {Stage::None, Stage::Add, Stage::Query, Stage::Connected, Stage::Names}) {
        bool cancelled = stage == Stage::None; Client fake(depth); fake.cancelled = &cancelled; fake.cancelAt = stage;
        result = resolution::Request(&fake, DefaultSettings, depth, &cancelled);
        Check(result.result == E_ABORT && !result.attempted && !fake.calls && fake.refs == 1 && !depth,
            "closing before mutation cancels even during COM preparation");
    }
}
void ReadbackTests() {
    unsigned depth = 0; Client client(depth);
    auto read = resolution::ReadProperties(&client, depth);
    Check(read.result == S_OK && read.known && read.width == 1920 && read.height == 1080 && !client.calls,
        "readback reports control values without a resize mutation");
    Check(client.reads == 2 && client.guarded && client.refs == 1 && !depth, "property reads balance guard");
    client.propertyType = VT_I2; read = resolution::ReadProperties(&client, depth);
    Check(read.result == DISP_E_TYPEMISMATCH && !read.known, "property LONG type required exactly");
    client.propertyType = VT_I4; client.width = -1;
    Check(!resolution::ReadProperties(&client, depth).known, "invalid readback cannot become known");
    client.width = 1920; client.readResult = E_FAIL;
    Check(resolution::ReadProperties(&client, depth).result == E_FAIL, "read error is preserved");
    Check(resolution::ReadProperties(nullptr, depth).result == E_POINTER, "null readback client rejected");
    depth = 1; Check(resolution::ReadProperties(&client, depth).result == E_PENDING, "nested readback deferred"); depth = 0;
    for (const auto stage : {Stage::Add, Stage::Width, Stage::Height}) {
        bool cancelled = false; Client fake(depth); fake.cancelled = &cancelled; fake.cancelAt = stage;
        read = resolution::ReadProperties(&fake, depth, &cancelled);
        Check(read.result == E_ABORT && !read.known && fake.refs == 1 && !depth, "readback cancellation cannot claim known values");
    }
}
void StateTests() {
    using resolution::Phase;
    const resolution::Result accepted{S_OK, true, true, true};
    resolution::Pending pending; Event match(1920, 1080), other(1024, 768);
    Check(!pending.ObserveEvent(&match.parameters, 99), "unsolicited event cannot confirm a request");
    Check(pending.Begin(DefaultSettings, 100) && !pending.eventObserved && pending.phase == Phase::Awaiting,
        "begin discards old event evidence before dispatch");
    Check(!pending.Begin(DefaultSettings, 101), "in-flight request cannot be overwritten");
    pending.Complete(accepted, 101);
    Check(pending.phase == Phase::Awaiting, "S_OK alone never confirms remote size");
    Check(!pending.ObserveEvent(&other.parameters, 102) && pending.phase == Phase::Awaiting, "wrong remote size remains pending");
    Check(pending.ObserveEvent(&match.parameters, 103) && pending.phase == Phase::Confirmed, "subsequent matching remote event confirms accepted request");
    pending.Begin(DefaultSettings, 200);
    Check(pending.ObserveEvent(&match.parameters, 201) && pending.phase == Phase::Awaiting, "synchronous event awaits method result");
    pending.Complete({E_ACCESSDENIED, true, true, false}, 202);
    Check(pending.phase == Phase::Failed, "synchronous matching event cannot hide method failure");
    pending.Begin(DefaultSettings, 300); pending.ObserveEvent(&match.parameters, 301);
    pending.ObserveEvent(&other.parameters, 302); pending.Complete(accepted, 303);
    Check(pending.phase == Phase::Awaiting && !pending.eventObserved, "later wrong size invalidates early candidate");
    pending.Tick(10300); Check(pending.phase == Phase::TimedOut, "observation deadline expires exactly");
    Check(!pending.ObserveEvent(&match.parameters, 10301) && pending.phase == Phase::TimedOut, "late event cannot revive timeout");
    pending.Begin(DefaultSettings, 20000); pending.ObserveEvent(&match.parameters, 20001); pending.Complete(accepted, 30000);
    Check(pending.phase == Phase::TimedOut, "slow COM return cannot confirm past deadline");
    pending.Begin(DefaultSettings, 40000); pending.Complete(accepted, 40001);
    Check(!pending.ObserveEvent(&match.parameters, 50000) && pending.phase == Phase::TimedOut, "late event checks deadline even without timer ticks");
    pending.Begin(DefaultSettings, 60000); pending.Tick(59999);
    Check(pending.phase == Phase::Failed, "clock reversal cannot extend confirmation window");
    pending.Begin(DefaultSettings, 70000); pending.Cancel(); pending.Complete(accepted, 70001);
    Check(pending.phase == Phase::Cancelled, "closing cannot be overwritten by late completion");
    for (const HRESULT missing : {E_NOINTERFACE, DISP_E_UNKNOWNNAME, DISP_E_MEMBERNOTFOUND}) {
        pending.Begin(DefaultSettings, 80000); pending.Complete({missing, false, false, false}, 80001);
        Check(pending.phase == Phase::Unsupported, "unsupported interface or method displayed honestly");
    }
    pending.Begin(DefaultSettings, 90000); pending.Complete({S_FALSE, true, true, false}, 90001);
    Check(pending.phase == Phase::Failed, "S_FALSE is not resize acceptance");
    pending.Begin(DefaultSettings, 100000);
    Check(!pending.ObserveEvent(nullptr, 100001), "missing event rejected");
    match.parameters.cArgs = 1; Check(!pending.ObserveEvent(&match.parameters, 100001), "truncated event rejected"); match.parameters.cArgs = 2;
    match.parameters.cNamedArgs = 1; Check(!pending.ObserveEvent(&match.parameters, 100001), "named size arguments rejected"); match.parameters.cNamedArgs = 0;
    match.arguments[0].vt = VT_I2; Check(!pending.ObserveEvent(&match.parameters, 100001), "size event requires exact LONG variants"); match.arguments[0].vt = VT_I4;
    match.arguments[0].lVal = -1; Check(!pending.ObserveEvent(&match.parameters, 100001), "negative remote size rejected"); match.arguments[0].lVal = 1080;
    pending.Cancel();
    unsigned depth = 0; Client fake(depth); fake.synchronous = &pending; pending.Begin(DefaultSettings, 100);
    const auto result = resolution::Request(&fake, DefaultSettings, depth); pending.Complete(result, 102);
    Check(pending.phase == Phase::Confirmed && fake.calls == 1 && fake.refs == 1 && !depth,
        "full fake COM request handles synchronous event with balanced guard");
}
}
int main() {
    SettingsTests(); DispatchTests(); ReadbackTests(); StateTests();
    std::cout << "Desktop resolution: " << passed << " passed, " << failed
        << " failed; fake COM only, no viewer, session or display change.\n";
    return failed ? 1 : 0;
}