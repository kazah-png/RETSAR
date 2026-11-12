# Guest session observation

[guest-session-observer.ps1](guest-session-observer.ps1) captures one read-only WTS snapshot inside the disposable Windows Server guest. It does not create, reconnect, disconnect, log off or claim ownership of any session. It cannot establish a physical second seat. The first guest execution produced a report but marked it incomplete with level/ID mismatches for two enumerated listeners. After explicit listener classification was added, complete baseline and connected snapshots were verified from the same guest boot. The original console was active in both with unchanged logon time, while the dedicated reference account had a distinct active session and new logon time. The candidate probe's exported identity also matched the account receipt's SID.

Controller loss prevented final same-boot enumeration in that first experiment. After recovery, a focused repeat produced complete baseline, connected and cleaned snapshots from one boot: the candidate was absent, active and absent respectively, and the original console was active at all three observations with unchanged logon time. Separate cleanup checks confirmed removal of the temporary account, credential and reference directory. The connection helper reached its observation timeout; these independently collected snapshots support the manually observed lifecycle, not normal helper completion, continuous physical isolation or ownership by a native provider. See the [reference experiment](RDP-REFERENCE.md). Host validation uses parsing and inert native-layout/classification checks, without WTS calls.

Windows PowerShell 5.1 parsing and compilation of the extracted C# declarations passed without running the script or calling WTS. The checked x64 sizes are 24 bytes for the enumeration entry, 224 for the level-one data and 232 for its wrapper; the data/logon-time offsets are 8/160. Twenty-four inert classification, diagnostic and incompleteness checks passed, including high IDs in non-listener states and unknown states. Three synthetic timestamp checks also passed. They are reproducible through [guest_session_observer_tests.ps1](../tests/guest_session_observer_tests.ps1). These pure checks are separate from the live guest snapshots and do not establish session creation or ownership.

## Environment and output

Before loading the native reader or querying sessions, the script requires `RETSARLAB`, QEMU as manufacturer, Windows Server, at most 5 GiB of memory, one or two logical processors and a 64-bit PowerShell process. These deployment checks prevent accidental execution on the normal host; they are not a security boundary against an administrator who changes the environment or script.

The output is JSON on standard output. `-Label` accepts one to 48 ASCII letters, digits, underscores or hyphens. Optional `-OutputPath` must name a **new `.json` file on guest C:** in an existing ordinary directory. Other drives, relative paths, UNC/device paths, alternate streams, traversal, reserved names and reparse parents are rejected before session enumeration. Existing files are never replaced. Parents are rechecked before creation; use trusted guest directories because this is not a defense against concurrent malicious directory replacement. No drive enumeration is performed. The host laboratory may reside on another approved volume; these rules concern paths inside the guest only.

Windows PowerShell can use temporary files when compiling the small C# reader. Both compiler temporary directories must be ordinary existing directories on guest C:. The observer changes no account, permission, service, session, registry or firewall setting. Its only requested persistent write is the optional new report. No passwords, credential material, tokens or client network addresses are collected. Reports contain laboratory account names and must be reviewed before publication.

## Evidence and limits

The reader uses only the local server handle with `WTSEnumerateSessionsW` and `WTSQuerySessionInformationW`. It frees their returned buffers with `WTSFreeMemory`. Session-query permissions are required; the observer does not change them or elevate itself. [Microsoft enumeration contract](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsenumeratesessionsw), [query contract](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsquerysessioninformationw).

At most 64 enumeration rows are processed. Non-listener rows are queried for their ID, state, station, reported domain/user, logon time, most recent connection/disconnection times and query timestamp. Times include a decimal FILETIME string to preserve integer precision and UTC text when valid; zero or negative values are unavailable. The Unicode structures and `WTSSessionInfoEx` constant follow the Windows SDK. [Microsoft extended-information fields](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ns-wtsapi32-wtsinfoex_level1_w).

Only an enumerated `WTSListen` state is classified as `kind=listener`. Microsoft defines this as a listener waiting for client requests, with no user logged on; it cannot become a regular client session. The row remains visible with its enumerated ID/state, `identityQueryStatus=not-applicable-listener`, `queryAttempted=false`, `querySucceeded=false` and null identity fields. No extended logon-identity query is made for it. Numeric IDs, including the high values observed in the initial report, never determine this classification. `WTSIdle`, disconnected, service and unknown states retain the normal query/error path. [Microsoft connection-state definitions](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ne-wtsapi32-wts_connectstate_class).

`incomplete` is true for a failed gate, compilation/non-listener-query/output error or truncated enumeration. Listener rows alone do not make the report incomplete and remain counted against the 64-row limit. A failed applicable query retains `queryAttempted=true`, `querySucceeded=false` and `identityQueryStatus=failed`; successful retrieval uses `identityQueryStatus=queried`. `errors` records bounded stages and numeric Win32 errors where available. Level/ID mismatches also include the requested `sessionId`, `returnedLevel`, `returnedBytes` and, only for level one, `returnedSessionId`. Unknown levels are not decoded as level-one data. No mismatch involving an applicable session is suppressed. A service session can legitimately have no user or logon time: successful retrieval does not make it suitable for comparing logons. Exit code 1 means incomplete evidence. A report-write failure is reported on stdout; do not treat a partially written file as valid evidence.

Enumeration and per-session queries are **not atomic**. Sessions can change or IDs can be reused between calls. A state change is flagged, but unchanged states cannot exclude a race. Domain/user are fixed-width display fields, not SIDs. The observer does not query user tokens, authentication LUIDs or process identities. `physical`, `sessionCreationProven` and `ownershipProven` are always false, even when all reads succeed. An empty snapshot is not proof that no other session exists.

## Verify inside the guest

Wait for installed-system and RDSH readiness to be independently verified. Transfer the script through the approved guest-media workflow. **Do not run these commands on the host.** In 64-bit PowerShell inside the guest, take a baseline before the separate reference-account/connection experiment:

```powershell
powershell.exe -NoProfile -File C:\RETSAR-Lab\guest-session-observer.ps1 -Label baseline -OutputPath C:\RETSAR-Lab\sessions-baseline.json
```

After the separately authorized reference connection, use a different label and new file:

```powershell
powershell.exe -NoProfile -File C:\RETSAR-Lab\guest-session-observer.ps1 -Label connected -OutputPath C:\RETSAR-Lab\sessions-connected.json
```

Check `environmentVerified=true`, `incomplete=false` and the expected guest boot time. Keep listener rows as explicit non-logon observations and compare identity only on successfully queried non-listener rows with a user and valid logon time. Record all errors rather than filtering them out. Repeat with new filenames after the reference experiment's disconnect/reconnect steps, if those steps are authorized. The observer itself performs neither step and does not authorize them. The reference account's independently recorded SID must remain separate from these display-name observations.

Compare observations only within the same boot and an explicitly recorded operation window:

| Observation | Supported interpretation |
| --- | --- |
| Dedicated account absent from a complete baseline, later present with a new valid logon time in the operation window | Candidate new logon to investigate; not proof that RETSAR created or owns it |
| Same ID, domain/user and valid logon time; a later connection time and expected state transition | Consistent with reconnection of an existing logon |
| Same ID but different user or logon time | Possible ID reuse or different logon; do not attach or clean up by that ID |
| Missing/invalid times, incomplete report, reboot or changing observations | Indeterminate; retain the evidence and repeat a controlled observation |

A definitive creation/ownership test still needs a cooperating probe in the exact session, an independently checked user SID and authentication identity, operation correlation, continued usability of the reference session and cleanup restricted to resources proven to belong to the experiment. This snapshot is evidence for that test, not its replacement. See [native-backend verification](../docs/NATIVE-BACKEND.md) and [remaining RDS integration gaps](../docs/RDS-INTEGRATION-GAPS.md). A normal Windows Server RDP reference does not demonstrate physical multiseat operation on the host.
