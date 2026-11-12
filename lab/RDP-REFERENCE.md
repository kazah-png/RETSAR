# Guest-local RDP reference experiment

This is a diagnostic reference for the disposable Windows Server guest. It tests whether the normal Microsoft RDP path can create a different user's session while the guest console remains available. It is not a RETSAR provider, a physical second seat, a Windows Home multi-session implementation, or evidence that Roblox will run in such a session.

Microsoft documents connecting `mstsc` to `localhost` from a Windows Server console as a troubleshooting test, with an explicit warning that it is unavailable in some configurations. A failure is a result to record, not a reason to change server restrictions. [Microsoft's localhost test](https://learn.microsoft.com/en-us/troubleshoot/windows-server/remote/remote-desktop-cannot-connect-remote-computer#verify-if-the-error-is-related-to-network)

## Scope and prerequisites

Run `guest-rdp-reference.ps1` only in 64-bit Windows PowerShell inside the elevated, active console of the QEMU guest named `RETSARLAB`, with Windows Server, no more than 5 GiB RAM and two logical processors. The script refuses other environments and requires `-GuestExperimentApproved`. Guest `TEMP` and `TMP` must be existing ordinary C: directories without reparse points before the native declarations are compiled. Its existing RDP listener must already be running with NLA and negotiated or TLS security. It does not install roles, change services/firewalls/policies, alter certificates, disable NLA, reset evaluation periods, or change licensing.

RDSH evaluation and licensing remain controlled by Windows. A diagnostic connection does not grant rights for production use or game workloads. There is no `/admin` connection or licensing workaround. [RDS licensing requirements](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-client-access-license)

The host, ASTER, host accounts, host credential storage, physical input and other drives are outside this experiment. Both RDP client and server are inside the guest. No host RDP client is opened.

## Run and observe

1. Take a complete, read-only guest session baseline using [the session observer](SESSION-OBSERVATION.md). Keep the console session's user, session ID and logon time. Its `retsar.guest-session-observation.v1` report must have `incomplete: false`; retain `guest.bootUtc` and the raw FILETIME values for comparison.
2. From an elevated Windows PowerShell console **inside the guest**, run:

   ```powershell
   .\guest-rdp-reference.ps1 -GuestExperimentApproved
   ```

3. The script creates the fixed standard account `RETSARRdpRef`, with a random password and a two-hour account expiry, and adds it to Users and Remote Desktop Users. A pre-existing account, state directory or Generic `TERMSRV/localhost` credential causes refusal; none is reused or overwritten.
4. The password is never printed, passed on a command line, written to a connection file, or placed on the clipboard. A temporary Generic credential is written only to the current guest console user's credential set with `CRED_PERSIST_SESSION`. The receipt contains the account SID, console owner SID, run marker and connection-file hash, not a password. Windows credential storage remains an authentication dependency; the script does not weaken delegation policy if the client refuses the credential. [Credential persistence](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw), [CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew)
5. `mstsc` opens a window inside the guest using `localhost`, NLA support and normal server-authentication warnings. Clipboard, disks, printers, serial ports, smart cards, WebAuthn, supported devices, cameras, audio and microphone redirection are disabled. Automatic reconnection is disabled. [RDP properties](https://learn.microsoft.com/en-us/azure/virtual-desktop/rdp-properties), [mstsc](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/mstsc)
6. Before opening the client, the script emits a `listener-certificate-reference` JSON record containing only the public `SSLCertificateSHA1Hash` already returned by the guest's listener query, if it is exactly 40 hexadecimal characters. Otherwise it reports `certificateAvailable: false` and a null fingerprint. If a certificate warning appears, inspect its certificate and compare its fingerprint with this listener reference. An unavailable reference does not authorize accepting the warning. No private key is read and no certificate is trusted automatically. Do not suppress warnings, save an exception blindly, replace certificates or lower authentication settings. If a credential prompt, policy rejection, licensing error, console-conflict error or other failure remains, cancel and record the exact non-secret error. Do not expose the generated password to work around it.
7. Take another complete observer snapshot during the connection attempt. A new active `RETSARLAB\RETSARRdpRef` session, distinct from the original console, together with the still-active original console identity/logon time, is the minimum session evidence. Opening the client, a successful credential write, NLA success and an RDP listener are not this evidence. Observer results establish identity and continuity, not ownership or cleanup authority.

The script waits at most five minutes for the client to exit. It then removes only a temporary credential whose target, type, session persistence, user and run marker still match. A timeout does not close any session or kill a client. The account and receipt remain for deliberate cleanup. An abrupt script/process termination may leave the session-scoped credential until cleanup or until the console user signs out; the account expiry is not a session-logoff mechanism.

## Cleanup

First sign out **inside the reference account's guest desktop**. Closing the RDP window only disconnects it and is not sufficient. Keep the original guest console logged in, close the diagnostic client there, then run:

```powershell
.\guest-rdp-reference.ps1 -Action Cleanup -GuestExperimentApproved
```

Cleanup requires the original console owner SID and a bounded receipt under the fixed guest directory `C:\RETSAR-Lab\RdpReference`. It revalidates the account SID and run description, the exact connection-file hash, ordinary non-reparse paths, and the absence of any reference-user session, including disconnected sessions. Any mismatch, unreadable session evidence or unexpected file stops cleanup. No session is ever disconnected or logged off by numeric ID. Only the matching credential, newly created account, connection file, receipt and empty experiment directory are removed. Windows-created profile data is not recursively deleted.

If preparation fails after account creation but before a usable receipt is written, the script compensates only the account returned by that creation call, after rechecking its SID/run marker and the absence of sessions, and only before launching any client. Partial files remain for review. If those checks fail, preserve the error and resources; do not fabricate a receipt or delete a similarly named account. The experiment refuses to reuse incomplete state. Host cleanup, broad account deletion, credential-store clearing and recursive profile deletion are not part of this script.

## Current validation boundary

The checked-in script is a prepared experiment. Local syntax checking does not execute its guest-only mutations and does not establish that localhost RDP or automatic credential use works in this guest build. A completed session observation and an independent process/token check are required before recording a real guest-session milestone. They still do not demonstrate a supported physical second seat on the user's Windows host.
