#requires -Version 5.1
# Guest-only boot diagnostics. This script never restarts Windows or services.
# Arm -> one separately authorized guest reboot -> Collect -> Cleanup.
[CmdletBinding()]
param(
    [ValidateSet('Arm','Collect','Cleanup')][string]$Action = 'Collect',
    [switch]$GuestExperimentApproved,
    [switch]$GuestManagementSession
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Assert-BootPath([string]$Path) {
    if($Path -cnotmatch '^C:\\RETSAR-Lab\\provider-activation(?:\\(?:receipt\.json|RETSAR-RdsBridge\.dll|bootreceipt\.json|bootreceipt\.tmp|bootactivation\.etl|bootactivation\.xml|bootcollect\.json|bootsummary\.txt))?$') {
        throw 'Unexpected guest boot-trace path.'
    }
}
function Assert-BootReceipt($Receipt) {
    $id=[guid]::Empty
    if($Receipt.schema -cne 'retsar.provider-boot-trace.v1' -or
        $Receipt.runId -cnotmatch '^[a-f0-9]{32}$' -or
        $Receipt.traceName -cne ('RETSAR-ActivationBoot-'+$Receipt.runId) -or
        ![guid]::TryParseExact([string]$Receipt.traceGuid,'B',[ref]$id) -or $id -eq [guid]::Empty -or
        $Receipt.traceGuid -cne $id.ToString('B').ToUpperInvariant() -or
        $Receipt.activationReceiptSha256 -cnotmatch '^[A-F0-9]{64}$' -or
        $Receipt.dllSha256 -cnotmatch '^[A-F0-9]{64}$' -or
        $Receipt.providerGuid -cne '{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}' -or
        $Receipt.traceGuid -ceq $Receipt.providerGuid) {throw 'Invalid boot-trace ownership receipt.'}
    foreach($flag in @('armed','disarmed','traceStopped','registryRemoved')) {
        if($Receipt.$flag -isnot [bool]) {throw 'Invalid boot-trace state flag.'}
    }
}
function Get-ExpectedBootShape([string]$TraceGuid,[int]$Start) {
    if($Start -notin @(0,1)) {throw 'Invalid AutoLogger Start value.'}
    $provider='{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}'
    [ordered]@{values=@(
        [ordered]@{name='BufferSize';kind='DWord';data=64},
        [ordered]@{name='FileName';kind='String';data='C:\RETSAR-Lab\provider-activation\bootactivation.etl'},
        [ordered]@{name='FlushTimer';kind='DWord';data=1},
        [ordered]@{name='Guid';kind='String';data=$TraceGuid},
        [ordered]@{name='LogFileMode';kind='DWord';data=2},
        [ordered]@{name='MaxFileSize';kind='DWord';data=16},
        [ordered]@{name='MaximumBuffers';kind='DWord';data=8},
        [ordered]@{name='MinimumBuffers';kind='DWord';data=4},
        [ordered]@{name='Start';kind='DWord';data=$Start}
    );children=@([ordered]@{name=$provider;shape=[ordered]@{values=@(
        [ordered]@{name='Enabled';kind='DWord';data=1},
        [ordered]@{name='EnableLevel';kind='DWord';data=4},
        [ordered]@{name='MatchAnyKeyword';kind='QWord';data=[long]1}
    );children=@()}})}
}
function Get-OwnedBootStart($Actual,$Receipt) {
    $values=@($Actual.values|Where-Object {$_.name -ceq 'Start'})
    if($values.Count -ne 1 -or $values[0].kind -cne 'DWord' -or
        $values[0].data -isnot [int] -or $values[0].data -notin @(0,1)) {throw 'Invalid AutoLogger Start state.'}
    $start=[int]$values[0].data
    if($Receipt.disarmed -and $start -ne 0) {throw 'The AutoLogger was rearmed outside this operation.'}
    # Start=0 may also be an interrupted Arm or a disarm whose receipt write
    # was lost. The complete configuration must still match before any edit.
    return $start
}
function Assert-BootShape($Actual,$Expected) {
    if($null -eq $Actual) {throw 'The owned AutoLogger key is missing.'}
    # Windows adds this documented result. Retain it separately as evidence;
    # it does not authorize any additional provider or configuration change.
    $status=@($Actual.values | Where-Object {$_.name -ceq 'Status'})
    if($status.Count -gt 1 -or ($status.Count -eq 1 -and
        ($status[0].kind -cne 'DWord' -or $status[0].data -isnot [int]))) {
        throw 'Unexpected AutoLogger status representation.'
    }
    $children=@($Actual.children | ForEach-Object {
        if($_.name -ceq $Expected.children[0].name) {
            $providerStatus=@($_.shape.values | Where-Object {$_.name -ceq 'Status'})
            if($providerStatus.Count -gt 1 -or ($providerStatus.Count -eq 1 -and
                ($providerStatus[0].kind -cne 'DWord' -or $providerStatus[0].data -isnot [int]))) {
                throw 'Unexpected provider status representation.'
            }
            [ordered]@{name=$_.name;shape=[ordered]@{
                values=@($_.shape.values | Where-Object {$_.name -cne 'Status'});
                children=$_.shape.children}}
        } else {$_}
    })
    $withoutStatus=[ordered]@{values=@($Actual.values | Where-Object {$_.name -cne 'Status'});
        children=$children}
    if(($withoutStatus|ConvertTo-Json -Depth 12 -Compress) -cne ($Expected|ConvertTo-Json -Depth 12 -Compress)) {
        throw 'AutoLogger ownership check failed.'
    }
}

if(!$GuestExperimentApproved -or $env:COMPUTERNAME -cne 'RETSARLAB' -or ![Environment]::Is64BitProcess) {
    throw 'Explicit approval inside the 64-bit RETSARLAB guest is required.'
}
$computer=Get-CimInstance Win32_ComputerSystem
$os=Get-CimInstance Win32_OperatingSystem
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$callerSession=[Diagnostics.Process]::GetCurrentProcess().SessionId
$sender=Get-Variable PSSenderInfo -ValueOnly -ErrorAction SilentlyContinue
$management=$GuestManagementSession -and $callerSession -eq 0 -and
    $null -ne $sender -and $sender -is [System.Management.Automation.Remoting.PSSenderInfo]
$console=$callerSession -gt 0 -and $env:SESSIONNAME -ieq 'Console'
if($computer.Manufacturer -cne 'QEMU' -or $os.Caption -notmatch '^Microsoft Windows Server' -or
    $os.ProductType -eq 1 -or $computer.TotalPhysicalMemory -gt 5GB -or
    $computer.NumberOfLogicalProcessors -notin @(1,2) -or (!$management -and !$console) -or
    !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Requires an elevated console or explicitly approved authenticated management in the small Server guest.'
}
$root='C:\RETSAR-Lab\provider-activation'
$receiptPath=Join-Path $root 'bootreceipt.json'
$activationPath=Join-Path $root 'receipt.json'
$dll=Join-Path $root 'RETSAR-RdsBridge.dll'
$etl=Join-Path $root 'bootactivation.etl'
$xml=Join-Path $root 'bootactivation.xml'
$classId='{E09044AF-A721-4D68-9F49-4527E9E51376}'
$providerGuid='{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}'
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
$mutex=[Threading.Mutex]::new($false,'Local\RETSAR.ProviderActivation')
$locked=$false
function Assert-OrdinaryBootPath([string]$Path,[switch]$Temporary) {
    if(!$Temporary) {Assert-BootPath $Path}
    elseif($Path -cnotmatch '^C:\\[^:]+$' -or $Path -match '[\x00-\x1f]') {throw 'Guest temporary paths must be on C:.'}
    $current='C:\'
    foreach($part in $Path.Substring(3).Split('\')) {
        if(!$part -or $part -in @('.','..') -or $part -match '[. ]$') {throw 'Ambiguous path component.'}
        $current=Join-Path $current $part
        if((Test-Path -LiteralPath $current) -and
            ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Reparse points are forbidden.'
        }
    }
}
function Get-BootKeyShape([string]$Path) {
    $key=$registry.OpenSubKey($Path,$false)
    if(!$key) {return $null}
    try {
        [ordered]@{values=@($key.GetValueNames() | Sort-Object | ForEach-Object {
            [ordered]@{name=$_;kind=$key.GetValueKind($_).ToString();
                data=$key.GetValue($_,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}
        });children=@($key.GetSubKeyNames() | Sort-Object | ForEach-Object {
            [ordered]@{name=$_;shape=(Get-BootKeyShape ($Path+'\'+$_))}
        })}
    } finally {$key.Dispose()}
}
function Assert-ActivationRegistration {
    # Legacy activation receipts did not expose a connection controller.
    $controlEnabled=$false
    $controlProperty=$activation.PSObject.Properties['connectionControlEnabled']
    if($null -ne $controlProperty) {
        if($controlProperty.Value -isnot [bool]) {throw 'Activation connection-control flag must be a Boolean.'}
        $controlEnabled=$controlProperty.Value
    }
    $expected=[ordered]@{values=@([ordered]@{name='RETSARRunId';kind='String';data=$activation.runId});
        children=@([ordered]@{name='InprocServer32';shape=[ordered]@{values=@(
            [ordered]@{name='';kind='String';data=$dll},
            [ordered]@{name='ThreadingModel';kind='String';data='Free'});children=@()}})}
    $actual=Get-BootKeyShape ('SOFTWARE\Classes\CLSID\'+$classId)
    if(($actual|ConvertTo-Json -Depth 10 -Compress) -cne ($expected|ConvertTo-Json -Depth 10 -Compress)) {throw 'The provider COM registration changed.'}
    $listenerValues=@(
        [ordered]@{name='fEnableWinStation';kind='DWord';data=1},
        [ordered]@{name='LoadableProtocol_Object';kind='String';data=$classId})
    if($controlEnabled) {
        $listenerValues+=[ordered]@{name='RETSARControlEnabled';kind='DWord';data=1}
    }
    $listenerValues+=[ordered]@{name='RETSARRunId';kind='String';data=$activation.runId}
    $expected=[ordered]@{values=$listenerValues;children=@()}
    $actual=Get-BootKeyShape 'SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RETSAR-Lab'
    if(($actual|ConvertTo-Json -Depth 10 -Compress) -cne ($expected|ConvertTo-Json -Depth 10 -Compress)) {throw 'The provider listener registration changed.'}
}
function Write-BootReceipt {
    Assert-BootReceipt $script:receipt
    $temporary=Join-Path $root 'bootreceipt.tmp'
    Assert-OrdinaryBootPath $temporary
    [IO.File]::WriteAllText($temporary,($script:receipt|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $receiptPath -Force
}
foreach($path in @($root,$receiptPath,$activationPath,$dll,$etl,$xml)) {Assert-OrdinaryBootPath $path}
foreach($path in @($env:TEMP,$env:TMP)) {Assert-OrdinaryBootPath $path -Temporary}

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class RetsarBootTraceNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct Wnode {
        public uint BufferSize,ProviderId; public ulong HistoricalContext,TimeStamp;
        public Guid Guid; public uint ClientContext,Flags;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct Properties {
        public Wnode Wnode;
        public uint BufferSize,MinimumBuffers,MaximumBuffers,MaximumFileSize,LogFileMode,FlushTimer,EnableFlags;
        public int AgeLimit;
        public uint NumberOfBuffers,FreeBuffers,EventsLost,BuffersWritten,LogBuffersLost,RealTimeBuffersLost;
        public IntPtr LoggerThreadId; public uint LogFileNameOffset,LoggerNameOffset;
    }
    public sealed class TraceInfo {
        public bool Running; public uint Result; public string Name,FileName; public Guid Guid;
        public uint Mode,MaximumFileSize,EventsLost,LogBuffersLost,RealTimeBuffersLost;
    }
    [DllImport("advapi32.dll",CharSet=CharSet.Unicode,ExactSpelling=true)]
    static extern uint ControlTraceW(ulong trace,string name,IntPtr properties,uint code);
    public static string ReadBoundedString(IntPtr buffer,int capacity,uint offset) {
        int size=Marshal.SizeOf(typeof(Properties));
        if(buffer==IntPtr.Zero || offset<(uint)size || (offset&1)!=0 || offset>(uint)(capacity-2))
            throw new InvalidOperationException("Invalid ETW text offset.");
        int count=Math.Min(1024,(capacity-(int)offset)/2);
        for(int i=0;i<count;++i) if(Marshal.ReadInt16(buffer,(int)offset+i*2)==0)
            return Marshal.PtrToStringUni(IntPtr.Add(buffer,(int)offset),i);
        throw new InvalidOperationException("Unterminated ETW text.");
    }
    static TraceInfo Control(string name,uint code,Guid expectedGuid) {
        int size=Marshal.SizeOf(typeof(Properties)),capacity=size+4096;
        IntPtr buffer=Marshal.AllocHGlobal(capacity);
        try {
            Marshal.Copy(new byte[capacity],0,buffer,capacity);
            var p=new Properties();
            p.Wnode.BufferSize=(uint)capacity;p.Wnode.Guid=expectedGuid;
            p.LoggerNameOffset=(uint)size;p.LogFileNameOffset=(uint)(size+2048);
            Marshal.StructureToPtr(p,buffer,false);
            uint error=ControlTraceW(0,name,buffer,code);
            if(error==4201) return new TraceInfo {Running=false,Result=error};
            if(error!=0) throw new Win32Exception((int)error);
            p=(Properties)Marshal.PtrToStructure(buffer,typeof(Properties));
            return new TraceInfo {Running=code==0,Result=0,Guid=p.Wnode.Guid,
                Name=ReadBoundedString(buffer,capacity,p.LoggerNameOffset),
                FileName=ReadBoundedString(buffer,capacity,p.LogFileNameOffset),
                Mode=p.LogFileMode,MaximumFileSize=p.MaximumFileSize,
                EventsLost=p.EventsLost,LogBuffersLost=p.LogBuffersLost,RealTimeBuffersLost=p.RealTimeBuffersLost};
        } finally {Marshal.FreeHGlobal(buffer);}
    }
    public static TraceInfo Query(string name) {return Control(name,0,Guid.Empty);}
    public static bool IsOwned(TraceInfo info,string name,string file,Guid guid) {
        return info.Running && info.Result==0 && info.Guid==guid &&
            String.Equals(info.Name,name,StringComparison.Ordinal) &&
            String.Equals(info.FileName,file,StringComparison.OrdinalIgnoreCase) &&
            // ETW may add one documented hybrid-shutdown default to an
            // otherwise circular session. Keep every other mode bit rejected.
            (info.Mode==2 || info.Mode==0x00400002 || info.Mode==0x00800002) &&
            info.MaximumFileSize==16;
    }
    public static TraceInfo StopOwned(string name,string file,Guid guid) {
        var info=Query(name);
        if(!IsOwned(info,name,file,guid)) throw new InvalidOperationException("ETW trace identity does not match the receipt.");
        return Control(name,1,guid);
    }
}
'@
try {
    try {$locked=$mutex.WaitOne(0)} catch [Threading.AbandonedMutexException] {$locked=$true}
    if(!$locked) {throw 'Another provider operation is running.'}
    $activation=Get-Content -LiteralPath $activationPath -Raw | ConvertFrom-Json
    if($activation.schema -cne 'retsar.provider-activation.v1' -or $activation.runId -cnotmatch '^[a-f0-9]{32}$' -or
        $activation.classId -cne $classId -or $activation.providerGuid -cne $providerGuid -or
        $activation.registrationComplete -ne $true -or $activation.traceStopped -ne $true -or
        $activation.registryRemoved -ne $false -or $activation.dllRemoved -ne $false -or
        $activation.dllSha256 -cnotmatch '^[A-F0-9]{64}$') {throw 'A completed, collected activation attempt is required.'}
    if((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -cne $activation.dllSha256) {throw 'The provider DLL changed.'}
    Assert-ActivationRegistration
    $activationHash=(Get-FileHash -LiteralPath $activationPath -Algorithm SHA256).Hash
    $traceName='RETSAR-ActivationBoot-'+$activation.runId
    $keyPath='SYSTEM\CurrentControlSet\Control\WMI\Autologger\'+$traceName
    if($Action -ceq 'Arm') {
        foreach($name in @('bootreceipt.json','bootreceipt.tmp','bootactivation.etl','bootactivation.xml','bootcollect.json','bootsummary.txt')) {
            $path=Join-Path $root $name
            Assert-OrdinaryBootPath $path
            if(Test-Path -LiteralPath $path) {throw 'Boot evidence already exists; it will not be overwritten.'}
        }
        if((Get-BootKeyShape $keyPath) -or [RetsarBootTraceNative]::Query($traceName).Running) {throw 'The boot trace already exists.'}
        $script:receipt=[pscustomobject][ordered]@{
            schema='retsar.provider-boot-trace.v1';runId=$activation.runId;traceName=$traceName;
            traceGuid=[guid]::NewGuid().ToString('B').ToUpperInvariant();providerGuid=$providerGuid;
            activationReceiptSha256=$activationHash;dllSha256=$activation.dllSha256;
            bootAtArmUtc=$os.LastBootUpTime.ToUniversalTime().ToString('o');
            armed=$false;disarmed=$false;traceStopped=$false;registryRemoved=$false;
            startupStatus=$null;stopResult=$null;createdSeat=$false
        }
        Write-BootReceipt
        # https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-an-autologger-session
        # Write Start=0 first; Start=1 is the final registry mutation.
        $shape=Get-ExpectedBootShape $script:receipt.traceGuid 0
        $key=$registry.CreateSubKey($keyPath)
        try {foreach($value in $shape.values) {$key.SetValue($value.name,$value.data,[Microsoft.Win32.RegistryValueKind]$value.kind)}} finally {$key.Dispose()}
        $key=$registry.CreateSubKey($keyPath+'\'+$providerGuid)
        try {foreach($value in $shape.children[0].shape.values) {$key.SetValue($value.name,$value.data,[Microsoft.Win32.RegistryValueKind]$value.kind)}} finally {$key.Dispose()}
        Assert-BootShape (Get-BootKeyShape $keyPath) $shape
        $key=$registry.OpenSubKey($keyPath,$true)
        try {$key.SetValue('Start',1,[Microsoft.Win32.RegistryValueKind]::DWord)} finally {$key.Dispose()}
        $script:receipt.armed=$true
        Write-BootReceipt
        [ordered]@{phase='boot-trace-armed';traceName=$traceName;guestRebootPerformed=$false;
            next='Perform one separately approved guest reboot, then Collect before any further reboot.';
            createdSeat=$false}|ConvertTo-Json -Compress
    } else {
        $script:receipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        Assert-BootReceipt $script:receipt
        if($script:receipt.runId -cne $activation.runId -or $script:receipt.activationReceiptSha256 -cne $activationHash -or
            $script:receipt.dllSha256 -cne $activation.dllSha256) {throw 'Activation ownership changed after arming.'}
        if($script:receipt.registryRemoved) {throw 'The boot-trace registry was already removed.'}
        $shape=Get-BootKeyShape $keyPath
        $start=Get-OwnedBootStart $shape $script:receipt
        Assert-BootShape $shape (Get-ExpectedBootShape $script:receipt.traceGuid $start)
        $status=@($shape.values|Where-Object {$_.name -ceq 'Status'})
        if($status.Count) {$script:receipt.startupStatus=$status[0].data}
        $providerStatus=@($shape.children[0].shape.values|Where-Object {$_.name -ceq 'Status'})
        $providerStartupStatus=if($providerStatus.Count){$providerStatus[0].data}else{$null}
        # Disable persistence BEFORE stopping/decoding, including failed starts.
        $key=$registry.OpenSubKey($keyPath,$true)
        try {$key.SetValue('Start',0,[Microsoft.Win32.RegistryValueKind]::DWord)} finally {$key.Dispose()}
        $script:receipt.disarmed=$true
        Write-BootReceipt
        $query=[RetsarBootTraceNative]::Query($traceName)
        if($query.Running) {
            $script:receipt.stopResult=[RetsarBootTraceNative]::StopOwned($traceName,$etl,[guid]$script:receipt.traceGuid)
        } else {$script:receipt.stopResult=$query}
        if([RetsarBootTraceNative]::Query($traceName).Running) {throw 'The boot trace is still running.'}
        $script:receipt.traceStopped=$true
        Write-BootReceipt
        if($Action -ceq 'Collect') {
            if($os.LastBootUpTime.ToUniversalTime().ToString('o') -ceq $script:receipt.bootAtArmUtc) {
                throw 'No new guest boot was observed. The trace was safely disarmed.'
            }
            foreach($path in @($xml,(Join-Path $root 'bootcollect.json'),(Join-Path $root 'bootsummary.txt'))) {
                Assert-OrdinaryBootPath $path
                if(Test-Path -LiteralPath $path) {throw 'Existing decoded boot evidence will not be overwritten.'}
            }
            if(!(Test-Path -LiteralPath $etl -PathType Leaf)) {throw 'No boot ETL was produced; inspect startupStatus in the retained receipt.'}
            & (Join-Path $env:SystemRoot 'System32\tracerpt.exe') $etl -of XML -o $xml -summary (Join-Path $root 'bootsummary.txt') | Out-Null
            if($LASTEXITCODE -ne 0) {throw 'Boot trace decode failed; original ETL retained.'}
            $service=Get-CimInstance Win32_Service -Filter "Name='TermService'"
            $process=Get-Process -Id $service.ProcessId -ErrorAction Stop
            $modules=@($process.Modules)
            if(!$modules.Count) {throw 'Unable to inspect the service process modules.'}
            $report=[ordered]@{schema='retsar.provider-boot-collection.v1';runId=$activation.runId;
                bootChanged=$true;startupStatus=$script:receipt.startupStatus;providerStartupStatus=$providerStartupStatus;trace=$script:receipt.stopResult;
                serviceState=$service.State;servicePid=[int]$service.ProcessId;
                processStartUtc=$process.StartTime.ToUniversalTime().ToString('o');
                providerModuleObserved=@($modules|Where-Object {$_.FileName -ieq $dll}).Count -gt 0;
                etlSha256=(Get-FileHash -LiteralPath $etl -Algorithm SHA256).Hash;
                xmlSha256=(Get-FileHash -LiteralPath $xml -Algorithm SHA256).Hash;createdSeat=$false}
            [IO.File]::WriteAllText((Join-Path $root 'bootcollect.json'),($report|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
            $report|ConvertTo-Json -Depth 10 -Compress
        } else {
            Assert-BootShape (Get-BootKeyShape $keyPath) (Get-ExpectedBootShape $script:receipt.traceGuid 0)
            $registry.DeleteSubKeyTree($keyPath,$false)
            if(Get-BootKeyShape $keyPath) {throw 'Boot AutoLogger removal could not be verified.'}
            $script:receipt.registryRemoved=$true
            Write-BootReceipt
            [ordered]@{phase='owned-boot-autologger-removed';traceStopped=$true;evidenceRetained=$true;
                providerRegistrationRetained=$true;createdSeat=$false}|ConvertTo-Json -Compress
        }
    }
} finally {
    if($locked) {$mutex.ReleaseMutex()}
    $mutex.Dispose();$registry.Dispose()
}
