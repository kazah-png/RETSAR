#requires -Version 5.1
# Opt-in activation-only experiment INSIDE the disposable QEMU Server guest.
# Never run on an everyday host. No connection, account, driver or seat is created.
[CmdletBinding()]
param(
    [ValidateSet('Prepare','Collect','Cleanup')][string]$Action = 'Collect',
    [switch]$GuestExperimentApproved,
    [switch]$RestartGuestService,
    [switch]$GuestManagementSession,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedDllSha256
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Pure helpers are extracted by the inert tests; the script itself is not run.
function Assert-ActivationLeaf([string]$Path) {
    if ($Path -cnotmatch '^C:\\RETSAR-Lab\\provider-activation(?:\\(?:RETSAR-RdsBridge\.dll|receipt\.json|receipt\.tmp|activation\.etl|activation\.xml|collect\.json|summary\.txt))?$') {
        throw 'Only the fixed guest activation directory and known leaf files are permitted.'
    }
}
function Assert-ActivationReceipt($Receipt) {
    $parsed = [guid]::Empty
    if ($Receipt.schema -cne 'retsar.provider-activation.v1' -or
        ![guid]::TryParseExact([string]$Receipt.runId,'N',[ref]$parsed) -or
        $Receipt.runId -cnotmatch '^[a-f0-9]{32}$' -or
        $Receipt.traceName -cne ('RETSAR-Activation-' + $Receipt.runId) -or
        $Receipt.dllSha256 -cnotmatch '^[A-F0-9]{64}$' -or
        $Receipt.providerGuid -cne '{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}' -or
        $Receipt.classId -cne '{E09044AF-A721-4D68-9F49-4527E9E51376}' -or
        $Receipt.listener -cne 'RETSAR-Lab' -or
        $Receipt.rdpTcpSha256 -cnotmatch '^[A-F0-9]{64}$') {
        throw 'The activation receipt does not identify this exact experiment.'
    }
}
function Assert-OnlyGuestConsole($Sessions, [int]$ConsoleId, [bool]$Management = $false) {
    if ($ConsoleId -le 0 -or @($Sessions).Count -lt 2 -or @($Sessions).Count -gt 64) {
        throw 'Invalid or incomplete session baseline.'
    }
    $ids = @{}
    $consoleCount = 0
    foreach ($row in $Sessions) {
        if ([int]$row.Id -lt 0) { throw 'Negative session ID.' }
        if ($ids.ContainsKey([int]$row.Id)) { throw 'Duplicate session ID.' }
        $ids[[int]$row.Id] = $true
        if ($row.Id -eq $ConsoleId -and $row.State -eq 0) { $consoleCount++; continue }
        # A guest at its logon screen has a Connected console without a user.
        # Management may use it only after an authoritative, bounded identity
        # query succeeds. Missing evidence is never treated as an empty account.
        if ($row.Id -eq $ConsoleId -and $row.State -eq 1 -and $Management -and
            $row.ConsoleIdentityQueryAttempted -eq $true -and
            $row.ConsoleIdentityQuerySucceeded -eq $true -and $row.ConsoleUserEmpty -eq $true) {
            $consoleCount++; continue
        }
        if ($row.Id -eq 0 -and $row.State -eq 4) { continue }
        if ($row.State -eq 6) { continue }
        throw 'Another session exists; refusing to restart the guest service.'
    }
    if ($consoleCount -ne 1) { throw 'The guest console is neither Active nor verified Connected without a user.' }
}
function Assert-CallerSession([int]$CallerId,[int]$ConsoleId,[bool]$Management,[bool]$AuthenticatedSender,[bool]$ConsoleEnvironment) {
    if ($ConsoleId -le 0) { throw 'No physical-console session is available in the guest.' }
    if ($CallerId -eq $ConsoleId -and $ConsoleEnvironment) { return }
    if ($CallerId -eq 0 -and $Management -and $AuthenticatedSender) { return }
    throw 'Requires the guest console or explicitly approved authenticated PowerShell management in Session0.'
}
function Assert-RegistryShape($Actual, $Expected) {
    if ($null -eq $Actual -or $null -eq $Expected -or
        ($Actual | ConvertTo-Json -Depth 10 -Compress) -cne
        ($Expected | ConvertTo-Json -Depth 10 -Compress)) {
        throw 'Registry ownership check failed; nothing will be removed.'
    }
}

if (!$GuestExperimentApproved) { throw 'Explicit -GuestExperimentApproved is required.' }
if ($env:COMPUTERNAME -cne 'RETSARLAB' -or ![Environment]::Is64BitProcess) {
    throw 'Requires 64-bit Windows PowerShell inside RETSARLAB.'
}
$computer = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($computer.Manufacturer -cne 'QEMU' -or $os.Caption -notmatch '^Microsoft Windows Server' -or
    $os.ProductType -eq 1 -or $computer.TotalPhysicalMemory -gt 5GB -or
    $computer.NumberOfLogicalProcessors -notin @(1,2) -or
    !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Requires an elevated administrator in the small RETSARLAB QEMU Windows Server guest.'
}
if ($Action -in @('Prepare','Cleanup') -and !$RestartGuestService) {
    throw 'This action requires -RestartGuestService for the disposable guest only.'
}
$root = 'C:\RETSAR-Lab\provider-activation'
$dll = Join-Path $root 'RETSAR-RdsBridge.dll'
$receiptPath = Join-Path $root 'receipt.json'
$etl = Join-Path $root 'activation.etl'
$xml = Join-Path $root 'activation.xml'
$classId = '{E09044AF-A721-4D68-9F49-4527E9E51376}'
$providerGuid = '{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}'
$classKey = 'SOFTWARE\Classes\CLSID\' + $classId
$listenerKey = 'SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RETSAR-Lab'
$rdpKey = 'SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp'
$logman = Join-Path $env:SystemRoot 'System32\logman.exe'
$tracerpt = Join-Path $env:SystemRoot 'System32\tracerpt.exe'

function Assert-OrdinaryPath([string]$Path, [switch]$Temporary) {
    if (!$Temporary) { Assert-ActivationLeaf $Path }
    elseif ($Path -cnotmatch '^C:\\[^:]+$' -or $Path -match '[\x00-\x1f]') { throw 'Guest temporary paths must be on C:.' }
    $current = 'C:\'
    foreach ($part in $Path.Substring(3).Split('\')) {
        if (!$part -or $part -in @('.','..') -or $part -match '[. ]$') { throw 'Ambiguous path component.' }
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Reparse points are forbidden.'
            }
        }
    }
}
foreach ($path in @($root,$dll,$receiptPath,$etl,$xml)) { Assert-OrdinaryPath $path }
foreach ($path in @($env:TEMP,$env:TMP)) { Assert-OrdinaryPath $path -Temporary }
# Copy the reviewed DLL to this fixed guest directory before Prepare. The
# directory must contain only that file; no media drive is enumerated here.
if (!(Test-Path -LiteralPath $dll -PathType Leaf)) { throw 'The reviewed guest DLL is missing.' }

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class RetsarActivationNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct Row { public int Id; public IntPtr Name; public int State; }
    public sealed class Snapshot {
        public int Id; public int State;
        public bool ConsoleIdentityQueryAttempted, ConsoleIdentityQuerySucceeded, ConsoleUserEmpty;
        public int ConsoleIdentityWin32Error, ConsoleIdentityReturnedBytes;
    }
    [DllImport("kernel32.dll", ExactSpelling=true)]
    public static extern uint WTSGetActiveConsoleSessionId();
    [DllImport("wtsapi32.dll", ExactSpelling=true, SetLastError=true)]
    static extern bool WTSEnumerateSessionsW(IntPtr server, int reserved, int version, out IntPtr data, out int count);
    [DllImport("wtsapi32.dll", ExactSpelling=true)]
    static extern void WTSFreeMemory(IntPtr data);
    [DllImport("wtsapi32.dll", ExactSpelling=true, SetLastError=true)]
    static extern bool WTSQuerySessionInformationW(IntPtr server, int session, int info, out IntPtr data, out int bytes);
    // Pure bounded decoding, also exercised with synthetic unmanaged buffers.
    // No account name is constructed, retained or written to a report.
    public static void ClassifyConsoleIdentity(Snapshot row, bool succeeded, IntPtr data, int bytes, int error) {
        row.ConsoleIdentityQueryAttempted=true;
        row.ConsoleIdentityQuerySucceeded=false; row.ConsoleUserEmpty=false;
        row.ConsoleIdentityReturnedBytes=bytes; row.ConsoleIdentityWin32Error=error;
        if(!succeeded) return;
        if(data==IntPtr.Zero || bytes<2 || bytes>2048 || (bytes&1)!=0) {
            row.ConsoleIdentityWin32Error=13; return;
        }
        bool terminated=false;
        for(int offset=0;offset<bytes;offset+=2) {
            if(Marshal.ReadInt16(data,offset)==0) {terminated=true;break;}
        }
        if(!terminated) {row.ConsoleIdentityWin32Error=13;return;}
        row.ConsoleIdentityQuerySucceeded=true;row.ConsoleIdentityWin32Error=0;
        row.ConsoleUserEmpty=Marshal.ReadInt16(data,0)==0;
    }
    static void QueryConnectedConsoleIdentity(Snapshot row) {
        IntPtr data=IntPtr.Zero; int bytes=0;
        bool succeeded=WTSQuerySessionInformationW(IntPtr.Zero,row.Id,5,out data,out bytes);
        int error=succeeded?0:Marshal.GetLastWin32Error();
        try {ClassifyConsoleIdentity(row,succeeded,data,bytes,error);}
        finally {if(data!=IntPtr.Zero) WTSFreeMemory(data);}
    }
    public static Snapshot[] Sessions() {
        IntPtr data; int count;
        if (!WTSEnumerateSessionsW(IntPtr.Zero,0,1,out data,out count)) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            if (count < 1 || count > 64 || data == IntPtr.Zero) throw new InvalidOperationException("Incomplete WTS baseline.");
            var rows = new Snapshot[count];
            uint consoleId=WTSGetActiveConsoleSessionId();
            int size = Marshal.SizeOf(typeof(Row));
            for(int i=0;i<count;++i) {
                var row=(Row)Marshal.PtrToStructure(IntPtr.Add(data,checked(i*size)),typeof(Row));
                rows[i]=new Snapshot {Id=row.Id,State=row.State};
                if(row.Id>=0 && (uint)row.Id==consoleId && row.State==1) QueryConnectedConsoleIdentity(rows[i]);
            }
            return rows;
        } finally { if(data!=IntPtr.Zero) WTSFreeMemory(data); }
    }
}
'@
$consoleId = [int][RetsarActivationNative]::WTSGetActiveConsoleSessionId()
$callerSessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
$sender = Get-Variable -Name PSSenderInfo -ValueOnly -ErrorAction SilentlyContinue
$hasAuthenticatedSender = $null -ne $sender -and $sender -is [System.Management.Automation.Remoting.PSSenderInfo]
Assert-CallerSession $callerSessionId $consoleId ([bool]$GuestManagementSession) $hasAuthenticatedSender ($env:SESSIONNAME -ieq 'Console')
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,
    [Microsoft.Win32.RegistryView]::Registry64)
$mutex = [Threading.Mutex]::new($false, 'Local\RETSAR.ProviderActivation')
$locked = $false

function Get-KeyShape([string]$Path) {
    $key = $registry.OpenSubKey($Path, $false)
    if (!$key) { return $null }
    try {
        $values = @($key.GetValueNames() | Sort-Object | ForEach-Object {
            $kind = $key.GetValueKind($_).ToString()
            $data = $key.GetValue($_,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            [ordered]@{name=$_;kind=$kind;data=$data}
        })
        $children = @($key.GetSubKeyNames() | Sort-Object | ForEach-Object {
            [ordered]@{name=$_;shape=(Get-KeyShape ($Path + '\' + $_))}
        })
        return [ordered]@{values=$values;children=$children}
    } finally { $key.Dispose() }
}
function Get-RdpHash {
    $shape = Get-KeyShape $rdpKey
    if (!$shape) { throw 'The standard RDP listener is missing.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($shape | ConvertTo-Json -Depth 30 -Compress))
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')
    } finally { $sha.Dispose() }
}
function Get-ServiceEvidence {
    $service = Get-CimInstance Win32_Service -Filter "Name='TermService'"
    if (!$service -or $service.State -cne 'Running' -or $service.ProcessId -le 0) {
        throw 'TermService is not running with an identifiable process.'
    }
    $process = Get-Process -Id $service.ProcessId -ErrorAction Stop
    # Module enumeration failure must remain an error, not an "unloaded" result.
    $modules = @($process.Modules)
    if (!$modules.Count) { throw 'Unable to enumerate the service process modules.' }
    $loaded = @($modules | Where-Object { $_.FileName -ieq $dll }).Count -gt 0
    [ordered]@{name='TermService';state=$service.State;pid=[int]$service.ProcessId;
        processStartUtc=$process.StartTime.ToUniversalTime().ToString('o');providerModuleObserved=$loaded}
}
function Write-Receipt {
    Assert-ActivationReceipt $script:receipt
    $temporary = Join-Path $root 'receipt.tmp'
    Assert-OrdinaryPath $temporary
    [IO.File]::WriteAllText($temporary,($script:receipt | ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $receiptPath -Force
}
function Assert-OriginalRdp {
    if ((Get-RdpHash) -cne $script:receipt.rdpTcpSha256) { throw 'The standard RDP listener changed; preserve evidence and investigate.' }
}
function Restart-OwnedGuestService {
    Assert-OnlyGuestConsole ([RetsarActivationNative]::Sessions()) $consoleId ([bool]$GuestManagementSession)
    $service = Get-Service -Name TermService
    $dependents = @($service.DependentServices)
    $expectedNames = @($script:receipt.dependencies | ForEach-Object { $_.name } | Sort-Object)
    $actualNames = @($dependents | ForEach-Object { $_.Name } | Sort-Object)
    if (($actualNames -join '|') -cne ($expectedNames -join '|')) { throw 'The guest service dependencies changed.' }
    foreach ($dependency in $script:receipt.dependencies) {
        if ($dependency.status -cnotin @('Running','Stopped')) { throw 'Unsupported original dependent-service state.' }
    }
    foreach ($dependent in $dependents) {
        $dependent.Refresh()
        if ($dependent.Status -notin @('Running','Stopped') -or
            @($dependent.DependentServices | Where-Object { $_.Status -ne 'Stopped' }).Count) {
            throw 'Dependent-service state requires separate inspection.'
        }
        if ($dependent.Status -eq 'Running' -and !$dependent.CanStop) { throw 'A dependent service cannot be stopped.' }
    }
    # ServiceController methods request transitions without an unbounded cmdlet
    # wait. Every observed transition below has a separate 30-second limit.
    foreach ($dependent in $dependents) {
        if ($dependent.Status -eq 'Running') {
            $dependent.Stop()
            $dependent.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
        }
    }
    $service.Stop()
    $service.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))
    $service.Start()
    $service.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running,[TimeSpan]::FromSeconds(30))
    foreach ($dependency in $script:receipt.dependencies) {
        if ($dependency.status -ceq 'Running') {
            $dependent = Get-Service -Name $dependency.name
            $dependent.Start()
            $dependent.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Running,[TimeSpan]::FromSeconds(30))
        }
    }
    Assert-OriginalRdp
}
function Stop-OwnedTrace {
    if ($script:receipt.traceStopped) { return }
    if (!$script:receipt.traceStarted) { throw 'Trace creation was not acknowledged; inspect the retained receipt.' }
    $query = & $logman query $script:receipt.traceName -ets 2>&1
    if ($LASTEXITCODE -ne 0 -or ($query -join ' ').IndexOf($etl,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw 'Cannot verify the owned trace name and output path; refusing to stop it.'
    }
    & $logman stop $script:receipt.traceName -ets | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'The owned trace could not be stopped.' }
    $script:receipt.traceStopped = $true
    Write-Receipt
}
function Expected-ClassShape {
    [ordered]@{values=@([ordered]@{name='RETSARRunId';kind='String';data=$script:receipt.runId});
        children=@([ordered]@{name='InprocServer32';shape=[ordered]@{
            values=@([ordered]@{name='';kind='String';data=$dll},
                [ordered]@{name='ThreadingModel';kind='String';data='Free'});children=@()}})}
}
function Expected-ListenerShape {
    [ordered]@{values=@([ordered]@{name='LoadableProtocol_Object';kind='String';data=$classId},
        [ordered]@{name='RETSARRunId';kind='String';data=$script:receipt.runId});children=@()}
}
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another activation operation is running.' }
    if ($Action -ceq 'Prepare') {
        if (!$ExpectedDllSha256) { throw 'Prepare requires the independently checked DLL SHA256.' }
        if (@(Get-ChildItem -LiteralPath $root -Force | Where-Object { $_.Name -cne 'RETSAR-RdsBridge.dll' }).Count) {
            throw 'Prepare requires a new directory containing only the reviewed DLL.'
        }
        if ((Get-KeyShape $classKey) -or (Get-KeyShape $listenerKey)) { throw 'An experiment registry key already exists.' }
        $dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
        if ($dllHash -cne $ExpectedDllSha256.ToUpperInvariant()) { throw 'DLL hash mismatch.' }
        $baseline = [RetsarActivationNative]::Sessions()
        Assert-OnlyGuestConsole $baseline $consoleId ([bool]$GuestManagementSession)
        # The receipt controls later cleanup. Restrict the owned directory to
        # administrators and SYSTEM before writing any operation state.
        $acl = [Security.AccessControl.DirectorySecurity]::new()
        $acl.SetAccessRuleProtection($true,$false)
        $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
        $system = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
        $acl.SetOwner($administrators)
        foreach ($sid in @($administrators,$system)) {
            $rule = [Security.AccessControl.FileSystemAccessRule]::new($sid,
                [Security.AccessControl.FileSystemRights]::FullControl,
                ([Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit),
                [Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Allow)
            $acl.AddAccessRule($rule)
        }
        # TermService normally runs as NetworkService. Permit DLL loading, but
        # never allow that service identity to modify the receipt or binaries.
        $networkService = [Security.Principal.SecurityIdentifier]::new('S-1-5-20')
        $readRule = [Security.AccessControl.FileSystemAccessRule]::new($networkService,
            [Security.AccessControl.FileSystemRights]::ReadAndExecute,
            ([Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit),
            [Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Allow)
        $acl.AddAccessRule($readRule)
        Set-Acl -LiteralPath $root -AclObject $acl
        $runId = [guid]::NewGuid().ToString('N')
        $script:receipt = [pscustomobject][ordered]@{
            schema='retsar.provider-activation.v1';runId=$runId;classId=$classId;listener='RETSAR-Lab';
            providerGuid=$providerGuid;traceName=('RETSAR-Activation-' + $runId);dllSha256=$dllHash;
            rdpTcpSha256=(Get-RdpHash);consoleId=$consoleId;baseline=$baseline;
            callerSessionId=$callerSessionId;managementSession=[bool]$GuestManagementSession;
            serviceBefore=(Get-ServiceEvidence);serviceAfter=$null;serviceAfterCleanup=$null;
            dependencies=@((Get-Service -Name TermService).DependentServices | ForEach-Object {
                [ordered]@{name=$_.Name;status=$_.Status.ToString()}
            });traceStarted=$false;traceStopped=$false;registrationComplete=$false;
            registryRemoved=$false;dllRemoved=$false;cleanupComplete=$false;createdSeat=$false
        }
        Write-Receipt
        # Microsoft registration contract: own COM CLSID and listener only.
        # https://learn.microsoft.com/en-us/windows/win32/termserv/registering-the-custom-protocol
        $key = $registry.CreateSubKey($classKey)
        try { $key.SetValue('RETSARRunId',$runId,[Microsoft.Win32.RegistryValueKind]::String) } finally { $key.Dispose() }
        $key = $registry.CreateSubKey($classKey + '\InprocServer32')
        try {
            $key.SetValue('',$dll,[Microsoft.Win32.RegistryValueKind]::String)
            $key.SetValue('ThreadingModel','Free',[Microsoft.Win32.RegistryValueKind]::String)
        } finally { $key.Dispose() }
        $key = $registry.CreateSubKey($listenerKey)
        try {
            $key.SetValue('LoadableProtocol_Object',$classId,[Microsoft.Win32.RegistryValueKind]::String)
            $key.SetValue('RETSARRunId',$runId,[Microsoft.Win32.RegistryValueKind]::String)
        } finally { $key.Dispose() }
        Assert-RegistryShape (Get-KeyShape $classKey) (Expected-ClassShape)
        Assert-RegistryShape (Get-KeyShape $listenerKey) (Expected-ListenerShape)
        $script:receipt.registrationComplete = $true
        Write-Receipt
        & $logman create trace $script:receipt.traceName -o $etl -p $providerGuid 0x1 4 -f bincirc -max 16 -nb 2 8 -bs 64 -ets | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Trace creation failed; preserve the receipt and registration for investigation.' }
        $script:receipt.traceStarted = $true
        Write-Receipt
        Restart-OwnedGuestService
        $script:receipt.serviceAfter = Get-ServiceEvidence
        Write-Receipt
        [ordered]@{phase='activation-requested';traceName=$script:receipt.traceName;
            service=$script:receipt.serviceAfter;createdSeat=$false} | ConvertTo-Json -Depth 5 -Compress
    } else {
        if (!(Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'The experiment receipt is missing.' }
        $script:receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        Assert-ActivationReceipt $script:receipt
        if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -cne $script:receipt.dllSha256) { throw 'The owned DLL changed.' }
        Assert-OriginalRdp
        if (!$script:receipt.registryRemoved) {
            Assert-RegistryShape (Get-KeyShape $classKey) (Expected-ClassShape)
            Assert-RegistryShape (Get-KeyShape $listenerKey) (Expected-ListenerShape)
        } elseif ((Get-KeyShape $classKey) -or (Get-KeyShape $listenerKey)) {
            throw 'A registry key reappeared after removal.'
        }
        Stop-OwnedTrace
        if ($Action -ceq 'Collect') {
            foreach ($outputPath in @($xml,(Join-Path $root 'summary.txt'),(Join-Path $root 'collect.json'))) {
                Assert-OrdinaryPath $outputPath
                if (Test-Path -LiteralPath $outputPath) { throw 'Existing decoded evidence will not be overwritten.' }
            }
            $service = Get-ServiceEvidence
            & $tracerpt $etl -of XML -o $xml -summary (Join-Path $root 'summary.txt') | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Trace decoding failed; retain the original ETL.' }
            $report = [ordered]@{schema='retsar.provider-activation-collection.v1';runId=$script:receipt.runId;
                providerGuid=$providerGuid;service=$service;createdSeat=$false;
                warning='A loaded module or trace event is activation evidence, not a session or physical seat.';
                etlSha256=(Get-FileHash -LiteralPath $etl -Algorithm SHA256).Hash;
                xmlSha256=(Get-FileHash -LiteralPath $xml -Algorithm SHA256).Hash;
                sessions=[RetsarActivationNative]::Sessions();rdpTcpUnchanged=$true}
            $output = Join-Path $root 'collect.json'
            Assert-OrdinaryPath $output
            if (Test-Path -LiteralPath $output) { throw 'Existing collection report will not be overwritten.' }
            [IO.File]::WriteAllText($output,($report | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
            $report | ConvertTo-Json -Depth 10 -Compress
        } else {
            Assert-OnlyGuestConsole ([RetsarActivationNative]::Sessions()) $consoleId ([bool]$GuestManagementSession)
            if (!$script:receipt.registryRemoved) {
                $registry.DeleteSubKeyTree($listenerKey,$false)
                $registry.DeleteSubKeyTree($classKey,$false)
                $script:receipt.registryRemoved = $true
                Write-Receipt
            }
            Restart-OwnedGuestService
            $script:receipt.serviceAfterCleanup = Get-ServiceEvidence
            if ($script:receipt.serviceAfterCleanup.providerModuleObserved) {
                throw 'The service still has the provider DLL loaded; files and receipt were retained.'
            }
            if ((Get-KeyShape $classKey) -or (Get-KeyShape $listenerKey)) { throw 'Registry cleanup verification failed.' }
            Assert-OrdinaryPath $dll
            Remove-Item -LiteralPath $dll
            $script:receipt.dllRemoved = !(Test-Path -LiteralPath $dll)
            if (!$script:receipt.dllRemoved) { throw 'DLL cleanup verification failed.' }
            $script:receipt.cleanupComplete = $true
            Write-Receipt
            [ordered]@{phase='owned-registration-and-dll-removed';rdpTcpUnchanged=$true;
                evidenceRetained=$true;createdSeat=$false} | ConvertTo-Json -Compress
        }
    }
} finally {
    $registry.Dispose()
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
