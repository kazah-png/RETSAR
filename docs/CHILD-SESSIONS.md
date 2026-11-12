# Child-session diagnostics

Windows documents a special loopback RDP child session tied to an existing user's session. Its ActiveX client must select `ConnectToChildSession` before connecting. Only one connected child session is supported by that documented model. This is a candidate for logical session isolation, not an API that assigns a physical monitor, keyboard or mouse. [Microsoft child-session contract](https://learn.microsoft.com/en-us/windows/win32/termserv/child-sessions).

## Implemented diagnostics

Build the optional executables with `.\build.ps1 -ChildSessionLab`. The build invokes help and injected-API tests only; it does not query or change Windows sessions. The ordinary `-Test` suite also includes those inert tests.

- `RETSAR-child-session-probe --status` queries enabled state, caller/console IDs and the current user's child-session ID. It records each API's success and error independently. A failed call returning Win32 error 1168 remains unknown; it is not rewritten as an authoritative absence result.
- `RETSAR-child-session-toggle --enable-read-restore --receipt <new-private-file>` is a separate explicit experiment. It requires a disabled baseline, caller equal to the active console, successful user-session inventory with no other user present, a new private receipt and flushed intent before changing the setting. It enables, reads back, restores disabled and reads back again, including after an enable or persistence failure. It never connects a client or creates a session.

The toggle's receipt destination is deliberately limited to the existing private laboratory tree on D:. It rejects existing files and reparse ancestors, then verifies the final opened handle before writing evidence or changing state. Ancestor checks are not an atomic namespace lock: an empty CREATE_NEW file can precede the final handle check. Do not describe this diagnostic as a race-proof general file writer. A RETSAR-local mutex serializes its probes, not unrelated applications changing the same global Windows setting.

A receipt line cannot attest to completion of the very write currently producing that line. The final persisted record therefore describes rollback/readback, while a zero process exit is the additional acknowledgement that all writes succeeded. Failed or incomplete persistence remains an error; restoration is separately reported.

## Observed target-edition result

On the observed Core-edition host, build 26300, the read-only query returned disabled. `WTSGetChildSessionId` failed with error 1168. The first toggle ran without an administrator token: enabling failed with error 5, while disabled readback and restoration succeeded. This did not establish an edition limitation.

After the user accepted Windows elevation, the same reviewed toggle enabled the setting, observed true, restored disabled and observed false. It exited 0. A separate read-only check is required after any further experiment. No child client was connected and no additional session was created by either toggle.

**A successful setting change does not prove that a child desktop will connect on Home.** Microsoft's Power Automate picture-in-picture product explicitly excludes Home; that product restriction is relevant evidence, but it must not be silently transformed into the return value of this separate Windows API experiment. The ActiveX connection and resulting session identity still require their own test. [Power Automate requirements](https://learn.microsoft.com/en-us/power-automate/desktop-flows/run-desktop-flows-pip).

## Verification and limits

The status suite passes 100 injected-API checks. The toggle suite passes 608 inert checks, including rejection before mutation, error/exception preservation, failed persistence, contradictory readbacks, restoration attempts and JSON output. These checks invoke no actual WTS API. They are distinct from the real target-edition toggle observations.

Neither diagnostic establishes concurrent desktop use, independent physical input, Roblox compatibility or a working RETSAR seat. No ordinary RDP connection, alternate account logon, session logoff or license-policy modification is implemented by these diagnostics.
