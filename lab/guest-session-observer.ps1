# Read-only session observation INSIDE the disposable guest. Never run on the host.
[CmdletBinding()]
param(
    [string]$Label = 'snapshot',
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$report = [ordered]@{
    schema = 'retsar.guest-session-observation.v1'
    label = $null
    startedUtc = [DateTime]::UtcNow.ToString('o')
    completedUtc = $null
    environmentVerified = $false
    readOnlySessionAccess = $true
    physical = $false
    sessionCreationProven = $false
    ownershipProven = $false
    incomplete = $true
    enumerationCount = 0
    limit = 64
    truncated = $false
    guest = $null
    evidence = @()
    errors = @()
}
$stage = 'arguments'
$outputValidated = $false

function Assert-GuestCPath([string]$Path, [switch]$Directory) {
    # Validate syntax before any filesystem lookup; never enumerate volumes.
    if ([string]::IsNullOrEmpty($Path) -or $Path.Length -gt 240 -or
        $Path -notmatch '^[Cc]:\\[A-Za-z0-9_~. -]+(?:\\[A-Za-z0-9_~. -]+)*$') {
        throw 'An ordinary absolute guest C: path is required.'
    }
    $parts = $Path.Substring(3).Split('\')
    foreach ($part in $parts) {
        if ($part -in '.', '..' -or $part -match '[. ]$' -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') {
            throw 'Unsafe path component.'
        }
    }
    $parentCount = $parts.Count
    if (!$Directory) { $parentCount-- }
    $current = 'C:\'
    for ($index = 0; $index -le $parentCount; $index++) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (!$item.PSIsContainer -or
            ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'An existing ordinary directory is required.'
        }
        if ($index -lt $parentCount) { $current = Join-Path $current $parts[$index] }
    }
    if (!$Directory -and (Test-Path -LiteralPath $Path)) {
        throw 'The output file must not already exist.'
    }
}

try {
    if ($Label -notmatch '^[A-Za-z0-9_-]{1,48}$') { throw 'Invalid observation label.' }
    $report.label = $Label
    $stage = 'environment-gate'
    # Refuse before CIM or WTS queries on a normally named host.
    if ($env:COMPUTERNAME -ne 'RETSARLAB') { throw 'Unexpected computer name.' }
    $computer = Get-CimInstance Win32_ComputerSystem
    $os = Get-CimInstance Win32_OperatingSystem
    if ($computer.Name -ne 'RETSARLAB' -or $computer.Manufacturer -ne 'QEMU' -or
        $os.Caption -notmatch '^Microsoft Windows Server' -or
        $computer.TotalPhysicalMemory -le 0 -or $computer.TotalPhysicalMemory -gt 5GB -or
        $computer.NumberOfLogicalProcessors -lt 1 -or $computer.NumberOfLogicalProcessors -gt 2 -or
        ![Environment]::Is64BitProcess) {
        throw 'This observer requires the bounded RETSARLAB QEMU Windows Server guest.'
    }
    $report.environmentVerified = $true
    $report.guest = [ordered]@{
        name = 'RETSARLAB'
        os = [string]$os.Caption
        bootUtc = $os.LastBootUpTime.ToUniversalTime().ToString('o')
        memoryBytes = [string]$computer.TotalPhysicalMemory
        logicalProcessors = [int]$computer.NumberOfLogicalProcessors
    }
    $stage = 'output-path'
    if ($OutputPath) {
        Assert-GuestCPath $OutputPath
        if ([IO.Path]::GetExtension($OutputPath) -ine '.json') { throw 'Use a new JSON file.' }
        $outputValidated = $true
    }
    # Windows PowerShell can compile Add-Type through temporary files.
    # Refuse redirected or non-C: compiler temporary directories before compilation.
    $stage = 'compiler-path'
    Assert-GuestCPath $env:TEMP -Directory
    Assert-GuestCPath $env:TMP -Directory
    $stage = 'native-compile'
    $nativeSource = @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;

namespace Retsar.GuestObservation {
    // Layouts and constants follow the Windows SDK wtsapi32.h, Unicode, pack 8.
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    public struct SessionEntry {
        public uint Id;
        public IntPtr Station;
        public int State;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
    public struct InfoLevel1 {
        public uint Id;
        public int State;
        public int Flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 33)] public string Station;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 21)] public string User;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 18)] public string Domain;
        public long LogonTime, ConnectTime, DisconnectTime, LastInputTime, CurrentTime;
        public uint IncomingBytes, OutgoingBytes, IncomingFrames, OutgoingFrames;
        public uint IncomingCompressedBytes, OutgoingCompressedBytes;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
    public struct InfoEx { public uint Level; public InfoLevel1 Data; }
    public sealed class TimeValue {
        public string raw100ns, utc, status;
        public TimeValue(long value) {
            raw100ns = value.ToString(CultureInfo.InvariantCulture);
            status = value <= 0 ? "unavailable" : "valid";
            if (value > 0) {
                try { utc = DateTime.FromFileTimeUtc(value).ToString("o", CultureInfo.InvariantCulture); }
                catch (ArgumentOutOfRangeException) { status = "invalid"; }
            }
        }
    }
    public sealed class Row {
        public uint sessionId;
        public int enumerationState, state;
        public string kind, identityQueryStatus, stateName, station, user, domain;
        public bool queryAttempted, querySucceeded, stateChangedDuringQuery;
        public TimeValue logonTime, connectTime, disconnectTime, sampledTime;
    }
    public sealed class Issue {
        public string stage;
        public uint? sessionId;
        public uint? returnedLevel, returnedSessionId, returnedBytes;
        public int win32Error;
        public Issue(string s, uint? id, int code) { stage = s; sessionId = id; win32Error = code; }
    }
    public sealed class Snapshot {
        public uint enumerationCount;
        public bool truncated;
        public List<Row> evidence = new List<Row>();
        public List<Issue> errors = new List<Issue>();
        public bool incomplete { get { return truncated || errors.Count != 0; } }
    }
    public static class Reader {
        [DllImport("wtsapi32.dll", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WTSEnumerateSessionsW(IntPtr server, uint reserved,
            uint version, out IntPtr buffer, out uint count);
        [DllImport("wtsapi32.dll", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WTSQuerySessionInformationW(IntPtr server, uint id,
            int infoClass, out IntPtr buffer, out uint bytes);
        [DllImport("wtsapi32.dll", ExactSpelling = true)]
        private static extern void WTSFreeMemory(IntPtr buffer);

        public static Snapshot Capture() {
            var result = new Snapshot();
            IntPtr entries = IntPtr.Zero;
            uint count;
            try {
                if (!WTSEnumerateSessionsW(IntPtr.Zero, 0, 1, out entries, out count)) {
                    result.errors.Add(new Issue("enumerate", null, Marshal.GetLastWin32Error()));
                    return result;
                }
                result.enumerationCount = count;
                result.truncated = count > 64;
                if (entries == IntPtr.Zero && count != 0) {
                    result.errors.Add(new Issue("enumerate-null-buffer", null, 0));
                    return result;
                }
                int stride = Marshal.SizeOf(typeof(SessionEntry));
                for (int i = 0; i < Math.Min(count, 64u); ++i) {
                    var entry = (SessionEntry)Marshal.PtrToStructure(
                        IntPtr.Add(entries, i * stride), typeof(SessionEntry));
                    var row = ClassifyEntry(entry.Id, entry.State);
                    result.evidence.Add(row);
                    // WTSListen denotes a listener with no logged-on user. Keep the
                    // enumeration evidence; no logon-identity query applies to it.
                    if (row.kind == "listener") continue;
                    IntPtr buffer = IntPtr.Zero;
                    uint bytes;
                    try {
                        row.queryAttempted = true;
                        row.identityQueryStatus = "failed";
                        // WTSSessionInfoEx is WTS_INFO_CLASS value 25.
                        if (!WTSQuerySessionInformationW(IntPtr.Zero, entry.Id, 25, out buffer, out bytes)) {
                            result.errors.Add(new Issue("query-info-ex", entry.Id, Marshal.GetLastWin32Error()));
                            continue;
                        }
                        if (buffer == IntPtr.Zero || bytes < Marshal.SizeOf(typeof(InfoEx))) {
                            result.errors.Add(new Issue("query-short-buffer", entry.Id, 0) {
                                returnedBytes = bytes,
                                returnedLevel = buffer != IntPtr.Zero && bytes >= 4
                                    ? (uint?)unchecked((uint)Marshal.ReadInt32(buffer)) : null });
                            continue;
                        }
                        uint level = unchecked((uint)Marshal.ReadInt32(buffer));
                        if (level != 1) {
                            result.errors.Add(ValidateInfoHeader(entry.Id, level, null, bytes));
                            continue;
                        }
                        var info = (InfoEx)Marshal.PtrToStructure(buffer, typeof(InfoEx));
                        var headerIssue = ValidateInfoHeader(entry.Id, info.Level, info.Data.Id, bytes);
                        if (headerIssue != null) {
                            result.errors.Add(headerIssue);
                            continue;
                        }
                        row.querySucceeded = true;
                        row.identityQueryStatus = "queried";
                        row.state = info.Data.State;
                        row.stateName = StateName(row.state);
                        row.stateChangedDuringQuery = row.state != entry.State;
                        row.station = info.Data.Station;
                        row.user = info.Data.User;
                        row.domain = info.Data.Domain;
                        row.logonTime = new TimeValue(info.Data.LogonTime);
                        row.connectTime = new TimeValue(info.Data.ConnectTime);
                        row.disconnectTime = new TimeValue(info.Data.DisconnectTime);
                        row.sampledTime = new TimeValue(info.Data.CurrentTime);
                        if (row.logonTime.status == "invalid" || row.connectTime.status == "invalid" ||
                            row.disconnectTime.status == "invalid" || row.sampledTime.status == "invalid") {
                            result.errors.Add(new Issue("invalid-time", entry.Id, 0));
                        }
                    } finally { if (buffer != IntPtr.Zero) WTSFreeMemory(buffer); }
                }
            } finally { if (entries != IntPtr.Zero) WTSFreeMemory(entries); }
            return result;
        }
        // Pure helpers also support inert classification/diagnostic checks.
        public static Row ClassifyEntry(uint id, int state) {
            bool listener = state == 6; // WTSListen, independent of the numeric session ID.
            return new Row { sessionId = id, enumerationState = state, state = state,
                stateName = StateName(state), kind = listener ? "listener" : "session",
                identityQueryStatus = listener ? "not-applicable-listener" : "not-queried" };
        }
        public static Issue ValidateInfoHeader(uint requestedId, uint level,
            uint? returnedId, uint bytes) {
            if (level == 1 && returnedId == requestedId) return null;
            return new Issue("query-level-or-id-mismatch", requestedId, 0) {
                returnedLevel = level, returnedSessionId = level == 1 ? returnedId : null,
                returnedBytes = bytes };
        }
        private static string StateName(int state) {
            string[] names = { "Active", "Connected", "ConnectQuery", "Shadow", "Disconnected",
                "Idle", "Listen", "Reset", "Down", "Init" };
            return state >= 0 && state < names.Length ? names[state] : "Unknown";
        }
    }
}
'@
    Add-Type -TypeDefinition $nativeSource -ErrorAction Stop
    $stage = 'native-layout'
    if ([Runtime.InteropServices.Marshal]::SizeOf([type][Retsar.GuestObservation.SessionEntry]) -ne 24 -or
        [Runtime.InteropServices.Marshal]::SizeOf([type][Retsar.GuestObservation.InfoLevel1]) -ne 224 -or
        [Runtime.InteropServices.Marshal]::SizeOf([type][Retsar.GuestObservation.InfoEx]) -ne 232 -or
        [Runtime.InteropServices.Marshal]::OffsetOf([type][Retsar.GuestObservation.InfoEx], 'Data').ToInt32() -ne 8 -or
        [Runtime.InteropServices.Marshal]::OffsetOf([type][Retsar.GuestObservation.InfoLevel1], 'LogonTime').ToInt32() -ne 160) {
        throw 'Unexpected native structure layout.'
    }
    $stage = 'session-query'
    $snapshot = [Retsar.GuestObservation.Reader]::Capture()
    $report.enumerationCount = $snapshot.enumerationCount
    $report.truncated = $snapshot.truncated
    $report.evidence = @($snapshot.evidence.ToArray())
    $report.errors = @($snapshot.errors.ToArray())
    $report.incomplete = $snapshot.incomplete
} catch {
    # Fixed stages/codes avoid copying paths, credentials or unbounded exception text.
    $report.errors += [ordered]@{ stage = $stage; exceptionType = $_.Exception.GetType().Name }
    $report.incomplete = $true
}
$report.completedUtc = [DateTime]::UtcNow.ToString('o')
$json = $report | ConvertTo-Json -Depth 8
if ($outputValidated -and $report.environmentVerified) {
    try {
        # Recheck parents, and CreateNew refuses overwrite even if a file races this call.
        Assert-GuestCPath $OutputPath
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
        $stream = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
    } catch {
        $report.errors += [ordered]@{ stage = 'output-write'; exceptionType = $_.Exception.GetType().Name }
        $report.incomplete = $true
        $json = $report | ConvertTo-Json -Depth 8
    }
}
$json
if ($report.incomplete) { exit 1 }
