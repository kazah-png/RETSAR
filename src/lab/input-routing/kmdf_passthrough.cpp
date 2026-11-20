// RETSAR original inert KMDF class-service adapter. No installation manifest,
// activation IOCTL, input logging, broker endpoint, or runtime routing is shipped.
#include <ntddk.h>
#include <wdf.h>
#include <kbdmou.h>
#include <ntddkbd.h>
#include <ntddmou.h>

#ifndef RETSAR_ENABLE_PHYSICAL_ROUTING
#define RETSAR_ENABLE_PHYSICAL_ROUTING 0
#endif
#if RETSAR_ENABLE_PHYSICAL_ROUTING
#error Physical routing is unavailable until authenticated delivery and recovery are validated.
#endif

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD RetsarDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL RetsarInternalControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE RetsarConnectComplete;

struct RetsarDeviceContext {
    CONNECT_DATA upper;
    volatile LONG kind; // 0 unconnected, 1 keyboard, 2 mouse.
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RetsarDeviceContext, RetsarContext)
using KeyboardService = void(*)(PDEVICE_OBJECT, PKEYBOARD_INPUT_DATA, PKEYBOARD_INPUT_DATA, PULONG);
using MouseService = void(*)(PDEVICE_OBJECT, PMOUSE_INPUT_DATA, PMOUSE_INPUT_DATA, PULONG);

// A real routing integration must preserve consumed-prefix accounting and split
// mixed mouse packets safely; it cannot substitute the policy decision for the
// entire Windows packet. Here the exact original range and count are forwarded.
void RetsarKeyboard(PDEVICE_OBJECT device, PKEYBOARD_INPUT_DATA first,
    PKEYBOARD_INPUT_DATA last, PULONG consumed) {
    auto* context = RetsarContext(WdfWdmDeviceGetWdfDeviceHandle(device));
    reinterpret_cast<KeyboardService>(context->upper.ClassService)(
        context->upper.ClassDeviceObject, first, last, consumed);
}
void RetsarMouse(PDEVICE_OBJECT device, PMOUSE_INPUT_DATA first,
    PMOUSE_INPUT_DATA last, PULONG consumed) {
    auto* context = RetsarContext(WdfWdmDeviceGetWdfDeviceHandle(device));
    reinterpret_cast<MouseService>(context->upper.ClassService)(
        context->upper.ClassDeviceObject, first, last, consumed);
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, RetsarDeviceAdd);
    return WdfDriverCreate(driver, registryPath, WDF_NO_OBJECT_ATTRIBUTES,
        &config, WDF_NO_HANDLE);
}
NTSTATUS RetsarDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT init) {
    UNREFERENCED_PARAMETER(driver);
    WdfFdoInitSetFilter(init);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RetsarDeviceContext);
    WDFDEVICE device = nullptr;
    auto result = WdfDeviceCreate(&init, &attributes, &device);
    if (!NT_SUCCESS(result)) return result;
    WDF_IO_QUEUE_CONFIG queue;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue, WdfIoQueueDispatchParallel);
    queue.EvtIoInternalDeviceControl = RetsarInternalControl;
    return WdfIoQueueCreate(device, &queue, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}
void RetsarConnectComplete(WDFREQUEST request, WDFIOTARGET target,
    PWDF_REQUEST_COMPLETION_PARAMS parameters, WDFCONTEXT completionContext) {
    UNREFERENCED_PARAMETER(target);
    auto* context = static_cast<RetsarDeviceContext*>(completionContext);
    if (!NT_SUCCESS(parameters->IoStatus.Status)) {
        CONNECT_DATA* connection = nullptr;
        if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(request, sizeof(CONNECT_DATA),
            reinterpret_cast<PVOID*>(&connection), nullptr)))
            *connection = context->upper; // A retry must never inherit our own callback.
        context->upper = {};
        InterlockedExchange(&context->kind, 0);
    }
    WdfRequestCompleteWithInformation(request, parameters->IoStatus.Status,
        parameters->IoStatus.Information);
}
void RetsarInternalControl(WDFQUEUE queue, WDFREQUEST request,
    size_t outputSize, size_t inputSize, ULONG code) {
    UNREFERENCED_PARAMETER(outputSize);
    UNREFERENCED_PARAMETER(inputSize);
    const auto device = WdfIoQueueGetDevice(queue);
    auto* context = RetsarContext(device);
    const LONG kind = code == IOCTL_INTERNAL_KEYBOARD_CONNECT ? 1 :
        code == IOCTL_INTERNAL_MOUSE_CONNECT ? 2 : 0;
    if (kind) {
        CONNECT_DATA* connection = nullptr;
        auto result = WdfRequestRetrieveInputBuffer(request, sizeof(CONNECT_DATA),
            reinterpret_cast<PVOID*>(&connection), nullptr);
        if (!NT_SUCCESS(result)) { WdfRequestComplete(request, result); return; }
        if (!connection->ClassDeviceObject || !connection->ClassService) {
            WdfRequestComplete(request, STATUS_INVALID_PARAMETER); return;
        }
        if (InterlockedCompareExchange(&context->kind, kind, 0)) {
            WdfRequestComplete(request, STATUS_SHARING_VIOLATION); return;
        }
        context->upper = *connection;
        connection->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(device);
        connection->ClassService = kind == 1 ? reinterpret_cast<PVOID>(RetsarKeyboard) :
            reinterpret_cast<PVOID>(RetsarMouse);
        WdfRequestFormatRequestUsingCurrentType(request);
        WdfRequestSetCompletionRoutine(request, RetsarConnectComplete, context);
        if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), nullptr)) {
            const auto failure = WdfRequestGetStatus(request);
            *connection = context->upper;
            context->upper = {};
            InterlockedExchange(&context->kind, 0);
            WdfRequestComplete(request, failure);
        }
        return;
    }
    WdfRequestFormatRequestUsingCurrentType(request);
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if (!WdfRequestSend(request, WdfDeviceGetIoTarget(device), &options))
        WdfRequestComplete(request, WdfRequestGetStatus(request));
}