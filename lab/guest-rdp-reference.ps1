# This diagnostic runs only INSIDE the disposable RETSARLAB guest console.
# Preparation, a client window, and authentication are not evidence of a new seat.
[CmdletBinding()]
param(
    [ValidateSet('Connect', 'Cleanup')][string]$Action = 'Connect',
    [switch]$GuestExperimentApproved
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (!$GuestExperimentApproved) { throw 'Explicit -GuestExperimentApproved is required.' }
if ($env:COMPUTERNAME -cne 'RETSARLAB' -or ![Environment]::Is64BitProcess) {
    throw 'Requires 64-bit Windows PowerShell inside RETSARLAB.'
}
$computer = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if ($env:COMPUTERNAME -cne 'RETSARLAB' -or $computer.Manufacturer -cne 'QEMU' -or
    $os.Caption -notmatch '^Microsoft Windows Server' -or $os.ProductType -eq 1 -or
    $computer.TotalPhysicalMemory -gt 5GB -or $computer.NumberOfLogicalProcessors -gt 2 -or
    $computer.NumberOfLogicalProcessors -lt 1 -or $env:SESSIONNAME -ine 'Console' -or
    !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Requires the elevated console of the RETSARLAB QEMU Windows Server guest (at most 5 GiB / 2 CPUs).'
}

# Windows PowerShell may use temporary compiler files for Add-Type. Validate only
# the two explicit guest paths; do not enumerate drives or follow junctions.
foreach ($temporaryDirectory in @($env:TEMP, $env:TMP)) {
    if ([string]::IsNullOrEmpty($temporaryDirectory) -or $temporaryDirectory -cnotmatch '^[Cc]:\\' -or
        $temporaryDirectory.Substring(2).Contains(':') -or $temporaryDirectory -match '[\x00-\x1f]') {
        throw 'Guest TEMP and TMP must be ordinary absolute C: directories.'
    }
    $currentDirectory = 'C:\'
    foreach ($part in $temporaryDirectory.Substring(3).Split('\')) {
        if (!$part -or $part -in @('.', '..') -or $part.EndsWith('.') -or $part.EndsWith(' ')) {
            throw 'Guest temporary paths must not contain traversal or ambiguous components.'
        }
        $currentDirectory = Join-Path $currentDirectory $part
        $entry = Get-Item -LiteralPath $currentDirectory -Force
        if (!$entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Guest temporary directories must exist and contain no reparse points.'
        }
    }
}

# No native function below changes server settings, licensing, sessions or tokens.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class RetsarRdpReferenceNative {
    const string Target = "TERMSRV/localhost";
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct Credential {
        public uint Flags, Type;
        public string TargetName, Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint CredentialBlobSize;
        public IntPtr CredentialBlob;
        public uint Persist, AttributeCount;
        public IntPtr Attributes;
        public string TargetAlias, UserName;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Session { public int Id; public IntPtr Name; public int State; }
    [DllImport("kernel32.dll", ExactSpelling = true)]
    public static extern uint WTSGetActiveConsoleSessionId();
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
    static extern bool CredReadW(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
    static extern bool CredWriteW(ref Credential credential, uint flags);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
    static extern bool CredDeleteW(string target, uint type, uint flags);
    [DllImport("advapi32.dll", ExactSpelling = true)]
    static extern void CredFree(IntPtr buffer);
    [DllImport("wtsapi32.dll", SetLastError = true, ExactSpelling = true)]
    static extern bool WTSEnumerateSessionsW(IntPtr server, int reserved, int version, out IntPtr sessions, out int count);
    [DllImport("wtsapi32.dll", SetLastError = true, ExactSpelling = true)]
    static extern bool WTSQuerySessionInformationW(IntPtr server, int session, int info, out IntPtr buffer, out int bytes);
    [DllImport("wtsapi32.dll", ExactSpelling = true)]
    static extern void WTSFreeMemory(IntPtr buffer);

    static bool Read(out Credential credential) {
        IntPtr pointer;
        if (!CredReadW(Target, 1, 0, out pointer)) {
            int error = Marshal.GetLastWin32Error();
            if (error != 1168) throw new Win32Exception(error);
            credential = new Credential(); return false;
        }
        try { credential = (Credential)Marshal.PtrToStructure(pointer, typeof(Credential)); return true; }
        finally { CredFree(pointer); }
    }
    public static bool CredentialExists() { Credential c; return Read(out c); }
    public static void WriteCredential(string user, string marker, IntPtr secret, int bytes) {
        if (CredentialExists()) throw new InvalidOperationException("A localhost credential already exists; refusing replacement.");
        var c = new Credential { Type = 1, TargetName = Target, UserName = user, Comment = marker,
            Persist = 1, CredentialBlob = secret, CredentialBlobSize = checked((uint)bytes) };
        if (!CredWriteW(ref c, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static void RemoveOwnedCredential(string user, string marker) {
        Credential c;
        if (!Read(out c)) return;
        if (c.Type != 1 || c.Persist != 1 || c.Comment != marker ||
            !String.Equals(c.TargetName, Target, StringComparison.OrdinalIgnoreCase) ||
            !String.Equals(c.UserName, user, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The localhost credential no longer matches this run; refusing deletion.");
        if (!CredDeleteW(Target, 1, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    static string SessionText(int id, int info) {
        IntPtr buffer; int bytes;
        if (!WTSQuerySessionInformationW(IntPtr.Zero, id, info, out buffer, out bytes))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        try { return bytes > 1 ? Marshal.PtrToStringUni(buffer) : ""; }
        finally { WTSFreeMemory(buffer); }
    }
    public static bool HasUserSession(string domain, string user) {
        IntPtr buffer; int count;
        if (!WTSEnumerateSessionsW(IntPtr.Zero, 0, 1, out buffer, out count))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            int size = Marshal.SizeOf(typeof(Session));
            for (int i = 0; i < count; ++i) {
                var s = (Session)Marshal.PtrToStructure(IntPtr.Add(buffer, checked(i * size)), typeof(Session));
                string name = SessionText(s.Id, 5);
                if (String.Equals(name, user, StringComparison.OrdinalIgnoreCase)) {
                    if (!String.Equals(SessionText(s.Id, 7), domain, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException("A matching session name has an unexpected or unreadable domain.");
                    return true;
                }
            }
            return false;
        } finally { WTSFreeMemory(buffer); }
    }
}
'@
$consoleId = [RetsarRdpReferenceNative]::WTSGetActiveConsoleSessionId()
if ($consoleId -eq [uint32]::MaxValue -or [Diagnostics.Process]::GetCurrentProcess().SessionId -ne $consoleId) {
    throw 'The caller must remain in the active guest console, not an RDP session.'
}
$userName = 'RETSARRdpRef'
$qualifiedUser = 'RETSARLAB\RETSARRdpRef'
$work = 'C:\RETSAR-Lab\RdpReference'
$receiptPath = Join-Path $work 'receipt.json'
$rdpPath = Join-Path $work 'connection.rdp'
$mutex = [Threading.Mutex]::new($false, 'Local\RETSAR.RdpReference')
$locked = $false

function Assert-OrdinaryGuestPath([string]$Path) {
    if ($Path -cnotmatch '^C:\\RETSAR-Lab(?:\\RdpReference(?:\\(?:receipt\.json|connection\.rdp))?)?$') {
        throw 'Unexpected guest reference path.'
    }
    $current = 'C:\'
    foreach ($part in $Path.Substring(3).Split('\')) {
        $current = Join-Path $current $part
        try { $entry = Get-Item -LiteralPath $current -Force }
        catch [Management.Automation.ItemNotFoundException] { continue }
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Reparse points are not permitted in reference paths.'
        }
    }
}
function Write-NewText([string]$Path, [string]$Text) {
    Assert-OrdinaryGuestPath $Path
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $preamble = [Text.Encoding]::Unicode.GetPreamble()
        $stream.Write($preamble, 0, $preamble.Length)
        $bytes = [Text.Encoding]::Unicode.GetBytes($Text)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}
function Get-ReferenceUser { @(Get-LocalUser | Where-Object { $_.Name -ieq $userName }) }
function Write-Status([string]$Phase) {
    @{schema='retsar.rdp-reference.v1'; phase=$Phase; createdSeat=$false} | ConvertTo-Json -Compress
}

try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another guest reference operation is running.' }
    Assert-OrdinaryGuestPath $work
    if (!(Test-Path -LiteralPath 'C:\RETSAR-Lab' -PathType Container)) { throw 'The guest laboratory directory is missing.' }

    if ($Action -eq 'Cleanup') {
        Assert-OrdinaryGuestPath $receiptPath
        Assert-OrdinaryGuestPath $rdpPath
        if (!(Test-Path -LiteralPath $receiptPath -PathType Leaf) -or
            (Get-Item -LiteralPath $receiptPath).Length -gt 16384) { throw 'A bounded run receipt is required.' }
        $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        $parsedRun = [guid]::Empty
        if ($receipt.schema -cne 'retsar.rdp-reference-receipt.v1' -or
            ![guid]::TryParseExact($receipt.runId, 'N', [ref]$parsedRun) -or
            $receipt.ownerSid -cne $identity.User.Value -or $receipt.computer -cne 'RETSARLAB' -or
            $receipt.userName -cne $userName -or $receipt.sid -notmatch '^S-1-5-21-\d+-\d+-\d+-\d+$') {
            throw 'Receipt identity does not match this guest console owner.'
        }
        $marker = 'RETSAR RDP reference ' + $receipt.runId
        $users = @(Get-ReferenceUser)
        if ($users.Count -gt 1 -or ($users.Count -eq 1 -and
            ($users[0].SID.Value -cne $receipt.sid -or $users[0].Description -cne $marker))) {
            throw 'The account no longer matches the recorded SID and run marker.'
        }
        # Disconnected sessions also block cleanup. No logoff, disconnect or PID-based kill is used.
        if ([RetsarRdpReferenceNative]::HasUserSession('RETSARLAB', $userName)) {
            throw 'The reference account still has a session. Sign out inside that guest session before cleanup.'
        }
        $unexpected = @(Get-ChildItem -LiteralPath $work -Force | Where-Object { $_.Name -cnotin @('receipt.json','connection.rdp') })
        if ($unexpected.Count) { throw 'Unexpected resources exist; refusing cleanup.' }
        if (Test-Path -LiteralPath $rdpPath) {
            if ((Get-FileHash -LiteralPath $rdpPath -Algorithm SHA256).Hash -cne $receipt.rdpSha256) {
                throw 'The connection file changed; refusing cleanup.'
            }
        }
        [RetsarRdpReferenceNative]::RemoveOwnedCredential($qualifiedUser, $marker)
        if ($users.Count -eq 1) {
            $again = Get-LocalUser -SID ([Security.Principal.SecurityIdentifier]::new($receipt.sid))
            if ($again.Name -cne $userName -or $again.Description -cne $marker -or
                [RetsarRdpReferenceNative]::HasUserSession('RETSARLAB', $userName)) { throw 'Account identity/session changed during cleanup.' }
            Remove-LocalUser -SID $again.SID
        }
        if (Test-Path -LiteralPath $rdpPath) { Remove-Item -LiteralPath $rdpPath }
        Remove-Item -LiteralPath $receiptPath
        Remove-Item -LiteralPath $work
        Write-Status 'owned-account-and-reference-files-removed'
        return
    }

    if (@(Get-ReferenceUser).Count -or (Test-Path -LiteralPath $work) -or
        [RetsarRdpReferenceNative]::CredentialExists()) {
        throw 'A reference account, directory or localhost credential already exists; nothing will be reused or replaced.'
    }
    $listener = Get-CimInstance -Namespace root/cimv2/terminalservices -ClassName Win32_TSGeneralSetting -Filter "TerminalName='RDP-tcp'"
    if (!$listener -or $listener.UserAuthenticationRequired -ne 1 -or $listener.SecurityLayer -eq 0 -or
        (Get-Service TermService).Status -ne 'Running') {
        throw 'An existing running RDP listener with NLA and negotiated/TLS security is required; this script will not configure it.'
    }
    $certificateSha1 = [string]$listener.SSLCertificateSHA1Hash
    $certificateAvailable = $certificateSha1 -cmatch '\A[0-9A-Fa-f]{40}\z'
    if ($certificateAvailable) { $certificateSha1 = $certificateSha1.ToUpperInvariant() }
    else { $certificateSha1 = $null }
    @{schema='retsar.rdp-reference.v1';phase='listener-certificate-reference';
        certificateAvailable=$certificateAvailable;certificateSha1=$certificateSha1;createdSeat=$false} | ConvertTo-Json -Compress
    $clientPath = Join-Path $env:SystemRoot 'System32\mstsc.exe'
    if (!(Test-Path -LiteralPath $clientPath -PathType Leaf)) { throw 'The guest mstsc client is missing.' }
    $runId = [guid]::NewGuid().ToString('N')
    $marker = 'RETSAR RDP reference ' + $runId
    New-Item -ItemType Directory -Path $work | Out-Null
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18','S-1-5-32-544')) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $work -AclObject $acl
    $secret = [Security.SecureString]::new()
    $random = [byte[]]::new(24)
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    $credentialWritten = $false
    $account = $null
    $receiptWritten = $false
    $clientLaunched = $false
    try {
        $rng.GetBytes($random)
        foreach ($char in ('Aa1!' + [Convert]::ToBase64String($random)).ToCharArray()) { $secret.AppendChar($char) }
        $secret.MakeReadOnly()
        $account = New-LocalUser -Name $userName -Password $secret -Description $marker -AccountExpires (Get-Date).AddHours(2)
        $rdp = @"
full address:s:localhost
username:s:$qualifiedUser
administrative session:i:0
screen mode id:i:1
desktopwidth:i:1024
desktopheight:i:768
session bpp:i:16
use multimon:i:0
authentication level:i:2
enablecredsspsupport:i:1
prompt for credentials:i:0
autoreconnection enabled:i:0
redirectclipboard:i:0
redirectprinters:i:0
redirectcomports:i:0
redirectsmartcards:i:0
redirectwebauthn:i:0
redirectposdevices:i:0
drivestoredirect:s:
devicestoredirect:s:
usbdevicestoredirect:s:
camerastoredirect:s:
audiomode:i:2
audiocapturemode:i:0
keyboardhook:i:0
gatewayusagemethod:i:0
"@
        Write-NewText $rdpPath $rdp
        $receipt = [ordered]@{schema='retsar.rdp-reference-receipt.v1';runId=$runId;computer='RETSARLAB';
            ownerSid=$identity.User.Value;userName=$userName;sid=$account.SID.Value;
            rdpSha256=(Get-FileHash -LiteralPath $rdpPath -Algorithm SHA256).Hash}
        Write-NewText $receiptPath ($receipt | ConvertTo-Json)
        $receiptWritten = $true
        $freshAccount = Get-LocalUser -SID $account.SID
        if ($freshAccount.Name -cne $userName -or $freshAccount.Description -cne $marker) {
            throw 'The new account identity changed before group assignment.'
        }
        foreach ($groupSidText in @('S-1-5-32-545', 'S-1-5-32-555')) {
            $groupSid = [Security.Principal.SecurityIdentifier]::new($groupSidText)
            $members = @(Get-LocalGroupMember -SID $groupSid | Where-Object { $_.SID.Value -ceq $account.SID.Value })
            if (!$members.Count) { Add-LocalGroupMember -SID $groupSid -Member $freshAccount }
            $verified = @(Get-LocalGroupMember -SID $groupSid | Where-Object { $_.SID.Value -ceq $account.SID.Value })
            if ($verified.Count -ne 1) { throw 'The new account group membership could not be verified by SID.' }
        }
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToCoTaskMemUnicode($secret)
        try {
            [RetsarRdpReferenceNative]::WriteCredential($qualifiedUser, $marker, $pointer, ($secret.Length * 2))
            $credentialWritten = $true
        } finally { [Runtime.InteropServices.Marshal]::ZeroFreeCoTaskMemUnicode($pointer) }
        Write-Status 'temporary-account-prepared'
        # The only intentionally visible client belongs to this guest console.
        $client = Start-Process -FilePath $clientPath -ArgumentList ('"' + $rdpPath + '"') -WindowStyle Normal -PassThru
        $clientLaunched = $true
        try {
            if ($client.SessionId -ne $consoleId) { throw 'The client is not in the expected guest console.' }
            Write-Status 'client-opened-observation-required'
            if (!$client.WaitForExit(300000)) { throw 'Observation window expired. Credential removed; client/session must be closed inside the guest.' }
        } finally { $client.Dispose() }
        Write-Status 'client-exited-observation-required'
    } catch {
        # Compensate only the exact account returned by this New-LocalUser call,
        # before a client could start and before a usable receipt was written.
        # Leave partial files in place for review; never manufacture a receipt.
        if ($null -ne $account -and !$receiptWritten -and !$clientLaunched) {
            $freshAccount = Get-LocalUser -SID $account.SID
            if ($freshAccount.Name -cne $userName -or $freshAccount.SID.Value -cne $account.SID.Value -or
                $freshAccount.Description -cne $marker -or
                [RetsarRdpReferenceNative]::HasUserSession('RETSARLAB', $userName)) {
                throw 'Incomplete preparation: account identity or sessions changed; refusing compensation.'
            }
            Remove-LocalUser -SID $freshAccount.SID
            Write-Status 'unreceipted-new-account-compensated'
        }
        throw
    } finally {
        try {
            if ($credentialWritten) { [RetsarRdpReferenceNative]::RemoveOwnedCredential($qualifiedUser, $marker) }
        } finally {
            $secret.Dispose()
            [Array]::Clear($random, 0, $random.Length)
            $rng.Dispose()
        }
    }
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    $identity.Dispose()
}
