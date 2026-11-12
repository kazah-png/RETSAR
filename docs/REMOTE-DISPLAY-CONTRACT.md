# Remote display contract

`RemoteDisplayContract` is an original connection-owned implementation of the public `IWRdsWddmIddProps` interface. It delegates IUnknown identity/reference counting to its owner, validates a distinct remote hardware identifier, associates notifications with an actual session ID, owns the OnDriverLoad handle and closes it on unload/disconnect. A worker receives a duplicate rather than a borrowed handle, so concurrent teardown cannot invalidate that worker's copy.

The component is **not exposed by the current bridge**. No remote driver is installed or activated through it. The console `SwDeviceHolder` experiment is separate: a remote IDD requires an OS-created remote-session device and the appropriate remote flags. Relabeling the console device does not establish that contract. [Microsoft remote IDD requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds).

Input checks reject missing, aliased or invalid handles and a non-null beep handle. Even valid handles end in `DeviceAndSessionBindingUnverified`. They are never treated as evidence that a keyboard or mouse belongs to the session. `SendInput`, Raw Input observations and anonymous event handles do not supply the device binding required by `GetInputHandles`. [Input contract](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles).

The focused suite passes **39 inert checks** for COM identity, validation, callback state, handle ownership, duplication and concurrent teardown. Its anonymous event handles are test fixtures, not display or input devices. No session, driver installation, physical-output assignment or Windows Home support is established by those tests.
