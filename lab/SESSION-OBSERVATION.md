# Guest session observation

[guest-session-observer.ps1](guest-session-observer.ps1) captures one read-only WTS snapshot inside the disposable Windows Server guest. It does not create, reconnect, disconnect, log off or claim ownership of any session. It cannot establish a physical second seat. The observer has not yet been executed against the guest; host validation is limited to parsing and inert native-layout checks.

Windows PowerShell 5.1 parsing and compilation of the extracted C# declarations passed without running the script or calling WTS. The checked x64 sizes are 24 bytes for the enumeration entry, 224 for the level-one data and 232 for its wrapper; the data/logon-time offsets are 8/160. Three synthetic timestamp checks passed. These checks validate representation, not guest behavior or session creation.

## Environment and output

Before loading the native reader or querying sessions, the script requires `RETSARLAB`, QEMU as manufacturer, Windows Server, at most 5 GiB of memory, one or two logical processors and a 64-bit PowerShell process. These deployment checks prevent accidental execution on the normal host; they are not a security boundary against an administrator who changes the environment or script.

The output is JSON on standard output. `-Label` accepts one to 48 ASCII letters, digits, underscores or hyphens. Optional `-OutputPath` must name a **new `.json` file on guest C:** in an existing ordinary directory. Other drives, relative paths, UNC/device paths, alternate streams, traversal, reserved names and reparse parents are rejected before session enumeration. Existing files are never replaced. Parents are rechecked before creation; use trusted guest directories because this is not a defense against concurrent malicious directory replacement. No drive enumeration is performed. The host laboratory may reside on another approved volume; these rules concern paths inside the guest only.

Windows PowerShell can use temporary files when compiling the small C# reader. Both compiler temporary directories must be ordinary existing directories on guest C:. The observer changes no account, permission, service, session, registry or firewall setting. Its only requested persistent write is the optional new report. No passwords, credential material, tokens or client network addresses are collected. Reports contain laboratory account names and must be reviewed before publication.

## Evidence and limits

The reader uses only the local server handle with `WTSEnumerateSessionsW` and `WTSQuerySessionInformationW`. It frees their returned buffers with `WTSFreeMemory`. Session-query permissions are required; the observer does not change them or elevate itself. [Microsoft enumeration contract](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsenumeratesessionsw), [query contract](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsquerysessioninformationw).

At most 64 rows are queried. Each row contains the session ID, enumeration and query states, station, reported domain/user, logon time, most recent connection/disconnection times and query timestamp. Times include a decimal FILETIME string to preserve integer precision and UTC text when valid; zero or negative values are unavailable. The Unicode structures and `WTSSessionInfoEx` constant follow the Windows SDK. [Microsoft extended-information fields](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ns-wtsapi32-wtsinfoex_level1_w).

`incomplete` is true for a failed gate, compilation/query/output error or truncated enumeration; a failed query retains a row with `querySucceeded=false`. `errors` records bounded stages and numeric Win32 errors where available. A listener or service session can legitimately have no user or logon time: successful retrieval does not make it suitable for comparing logons. Exit code 1 means incomplete evidence. A report-write failure is reported on stdout; do not treat a partially written file as valid evidence.

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

Check `environmentVerified=true`, `incomplete=false` and the expected guest boot time. Record all errors rather than filtering them out. Repeat with new filenames after the reference experiment's disconnect/reconnect steps, if those steps are authorized. The observer itself performs neither step and does not authorize them. The reference account's independently recorded SID must remain separate from these display-name observations.

Compare observations only within the same boot and an explicitly recorded operation window:

| Observation | Supported interpretation |
| --- | --- |
| Dedicated account absent from a complete baseline, later present with a new valid logon time in the operation window | Candidate new logon to investigate; not proof that RETSAR created or owns it |
| Same ID, domain/user and valid logon time; a later connection time and expected state transition | Consistent with reconnection of an existing logon |
| Same ID but different user or logon time | Possible ID reuse or different logon; do not attach or clean up by that ID |
| Missing/invalid times, incomplete report, reboot or changing observations | Indeterminate; retain the evidence and repeat a controlled observation |

A definitive creation/ownership test still needs a cooperating probe in the exact session, an independently checked user SID and authentication identity, operation correlation, continued usability of the reference session and cleanup restricted to resources proven to belong to the experiment. This snapshot is evidence for that test, not its replacement. See [native-backend verification](../docs/NATIVE-BACKEND.md) and [remaining RDS integration gaps](../docs/RDS-INTEGRATION-GAPS.md). A normal Windows Server RDP reference does not demonstrate physical multiseat operation on the host.
