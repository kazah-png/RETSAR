#requires -Version 5.1
# Exercise pure validators and a synthetic-buffer decoder. Never execute the guest script,
# WTS functions, registry/service commands, ETW sessions or provider DLL loading.
$ErrorActionPreference = 'Stop'
$source = Join-Path (Split-Path -Parent $PSScriptRoot) 'lab\guest-provider-activation.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors.Message -join '; ') }
$names=@('Assert-ActivationLeaf','Assert-ActivationReceipt','Assert-OnlyGuestConsole','Assert-RegistryShape','Assert-CallerSession','Expected-ListenerShape')
foreach($name in $names) {
    $functions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if($functions.Count -ne 1) { throw ('Missing or duplicate pure validator: '+$name) }
    # Only the six reviewed function declarations are evaluated.
    . ([scriptblock]::Create($functions[0].Extent.Text))
}
$script:checks=0
function Pass([string]$Name) { $script:checks++; Write-Output ('PASS ACTIVATION: '+$Name) }
function Accept([string]$Name,[scriptblock]$Test) { & $Test; Pass $Name }
function Reject([string]$Name,[scriptblock]$Test) {
    $rejected=$false
    try { & $Test } catch { $rejected=$true }
    if(!$rejected) { throw ('FAIL ACTIVATION: accepted '+$Name) }
    Pass $Name
}
$valid=[pscustomobject][ordered]@{
    schema='retsar.provider-activation.v1';runId='0123456789abcdef0123456789abcdef';
    traceName='RETSAR-Activation-0123456789abcdef0123456789abcdef';
    dllSha256=('A'*64);rdpTcpSha256=('B'*64);
    providerGuid='{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}';
    classId='{E09044AF-A721-4D68-9F49-4527E9E51376}';listener='RETSAR-Lab'
}
Accept 'Exact ownership receipt' { Assert-ActivationReceipt $valid }
$changes=@(
    @('schema','other'),
    @('runId','invalid'),
    @('runId','FEDCBA9876543210FEDCBA9876543210'),
    @('traceName','RETSAR-Activation-another'),
    @('dllSha256',('A'*63)),
    @('dllSha256',('g'*64)),
    @('rdpTcpSha256',''),
    @('providerGuid','{00000000-0000-0000-0000-000000000000}'),
    @('classId','{00000000-0000-0000-0000-000000000000}'),
    @('listener','RDP-Tcp')
)
foreach($change in $changes) {
    $bad=$valid | ConvertTo-Json | ConvertFrom-Json
    $bad.($change[0])=$change[1]
    Reject ('Altered receipt '+$change[0]) { Assert-ActivationReceipt $bad }
}
Accept 'Fixed guest directory' { Assert-ActivationLeaf 'C:\RETSAR-Lab\provider-activation' }
Accept 'Reviewed DLL leaf' { Assert-ActivationLeaf 'C:\RETSAR-Lab\provider-activation\RETSAR-RdsBridge.dll' }
Accept 'ETL evidence leaf' { Assert-ActivationLeaf 'C:\RETSAR-Lab\provider-activation\activation.etl' }
foreach($path in @('D:\OtherLab\provider-activation','E:\file',
    'C:\RETSAR-Lab\provider-activation\..\other','C:\RETSAR-Lab\provider-activation\other.dll',
    'C:\RETSAR-Lab\provider-activation\activation.etl:stream','\\server\share',
    'C:\RETSAR-Lab\provider-activation-extra','C:\RETSAR-Lab\provider-activation\receipt.json\child')) {
    Reject ('Forbidden path '+$path) { Assert-ActivationLeaf $path }
}
Accept 'Active guest console caller' { Assert-CallerSession 1 1 $false $false $true }
Accept 'Authenticated Session0 management with explicit switch' { Assert-CallerSession 0 1 $true $true $false }
Reject 'Session0 without explicit management switch' { Assert-CallerSession 0 1 $false $true $false }
Reject 'Session0 without authenticated remoting sender' { Assert-CallerSession 0 1 $true $false $false }
Reject 'Different interactive caller session' { Assert-CallerSession 2 1 $true $true $false }
Reject 'No active console even with authenticated management' { Assert-CallerSession 0 0 $true $true $false }
Reject 'Same numeric console ID without console environment' { Assert-CallerSession 1 1 $false $false $false }
function Row([int]$Id,[int]$State) { [pscustomobject]@{Id=$Id;State=$State} }
$baseline=@((Row 0 4),(Row 1 0),(Row 65536 6),(Row 65537 6))
Accept 'Only current console, Services and listeners' { Assert-OnlyGuestConsole $baseline 1 }
Reject 'Another active user' { Assert-OnlyGuestConsole ($baseline+@(Row 2 0)) 1 }
Reject 'Another disconnected user' { Assert-OnlyGuestConsole ($baseline+@(Row 2 4)) 1 }
Reject 'Another idle session' { Assert-OnlyGuestConsole ($baseline+@(Row 2 5)) 1 }
Reject 'Unknown session state' { Assert-OnlyGuestConsole ($baseline+@(Row 2 42)) 1 }
Reject 'Missing current console' { Assert-OnlyGuestConsole @((Row 0 4),(Row 65536 6)) 1 }
Reject 'Console is disconnected' { Assert-OnlyGuestConsole @((Row 0 4),(Row 1 4)) 1 }
Reject 'Duplicate session ID' { Assert-OnlyGuestConsole ($baseline+@(Row 1 0)) 1 }
Reject 'Console is Session0' { Assert-OnlyGuestConsole $baseline 0 }
Reject 'Too many sessions' { Assert-OnlyGuestConsole (0..64 | ForEach-Object {Row $_ 6}) 1 }
Reject 'Negative listener ID' { Assert-OnlyGuestConsole ($baseline+@(Row -1 6)) 1 }
function ConnectedConsole([bool]$Attempted,[bool]$Succeeded,[bool]$Empty) {
    [pscustomobject]@{Id=1;State=1;ConsoleIdentityQueryAttempted=$Attempted;
        ConsoleIdentityQuerySucceeded=$Succeeded;ConsoleUserEmpty=$Empty}
}
$emptyConsole=@((Row 0 4),(ConnectedConsole $true $true $true),(Row 65536 6))
Accept 'Management with verified empty Connected console' { Assert-OnlyGuestConsole $emptyConsole 1 $true }
Reject 'Empty Connected console without management opt-in' { Assert-OnlyGuestConsole $emptyConsole 1 $false }
Reject 'Connected console has an account' {
    Assert-OnlyGuestConsole @((Row 0 4),(ConnectedConsole $true $true $false),(Row 65536 6)) 1 $true
}
Reject 'Connected console identity query failed' {
    Assert-OnlyGuestConsole @((Row 0 4),(ConnectedConsole $true $false $true),(Row 65536 6)) 1 $true
}
Reject 'Connected console identity query was not attempted' {
    Assert-OnlyGuestConsole @((Row 0 4),(ConnectedConsole $false $true $true),(Row 65536 6)) 1 $true
}
Reject 'Management with another Connected session' {
    Assert-OnlyGuestConsole ($emptyConsole+@(Row 2 1)) 1 $true
}
# Compile the embedded declarations but invoke only the pure buffer classifier.
# Neither session enumeration nor a WTS function is called by these tests.
$nativeDefinitions=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.StringConstantExpressionAst] -and
    $node.Value -like '*public static class RetsarActivationNative*'
},$true))
if($nativeDefinitions.Count -ne 1) {throw 'Expected one native declaration block.'}
Add-Type -TypeDefinition $nativeDefinitions[0].Value
$memory=[Runtime.InteropServices.Marshal]::AllocHGlobal(8)
try {
    $bytes=[byte[]]@(0,0,0,0,0,0,0,0)
    [Runtime.InteropServices.Marshal]::Copy($bytes,0,$memory,8)
    $result=[RetsarActivationNative+Snapshot]::new()
    [RetsarActivationNative]::ClassifyConsoleIdentity($result,$true,$memory,2,0)
    if(!$result.ConsoleIdentityQueryAttempted -or !$result.ConsoleIdentityQuerySucceeded -or
        !$result.ConsoleUserEmpty -or $result.ConsoleIdentityReturnedBytes -ne 2 -or $result.ConsoleIdentityWin32Error) {
        throw 'Valid empty WTS text was not classified correctly.'
    }
    Pass 'Empty terminated UTF16 console identity'
    [Runtime.InteropServices.Marshal]::WriteInt16($memory,0,65)
    [RetsarActivationNative]::ClassifyConsoleIdentity($result,$true,$memory,4,0)
    if(!$result.ConsoleIdentityQuerySucceeded -or $result.ConsoleUserEmpty) {throw 'Nonempty identity accepted as empty.'}
    Pass 'Nonempty console identity remains nonempty without recording its name'
    foreach($size in @(0,1,3,2050)) {
        [RetsarActivationNative]::ClassifyConsoleIdentity($result,$true,$memory,$size,0)
        if($result.ConsoleIdentityQuerySucceeded -or $result.ConsoleUserEmpty -or $result.ConsoleIdentityWin32Error -ne 13) {
            throw 'Invalid WTS byte bound was accepted.'
        }
        Pass ('Invalid WTS returned byte count '+$size)
    }
    [RetsarActivationNative]::ClassifyConsoleIdentity($result,$true,[IntPtr]::Zero,2,0)
    if($result.ConsoleIdentityQuerySucceeded -or $result.ConsoleIdentityWin32Error -ne 13) {throw 'Null successful buffer accepted.'}
    Pass 'Null WTS buffer rejected'
    [RetsarActivationNative]::ClassifyConsoleIdentity($result,$true,$memory,2,0)
    if($result.ConsoleIdentityQuerySucceeded -or $result.ConsoleIdentityWin32Error -ne 13) {throw 'Unterminated buffer accepted.'}
    Pass 'Unterminated WTS text rejected'
    [RetsarActivationNative]::ClassifyConsoleIdentity($result,$false,$memory,4,5)
    if($result.ConsoleIdentityQuerySucceeded -or $result.ConsoleUserEmpty -or $result.ConsoleIdentityWin32Error -ne 5) {
        throw 'WTS query error was not retained.'
    }
    Pass 'Failed WTS query retains original error'
} finally {[Runtime.InteropServices.Marshal]::FreeHGlobal($memory)}
$expected=[ordered]@{values=@([ordered]@{name='owner';kind='String';data='run'});children=@()}
Accept 'Exact registry shape' { Assert-RegistryShape $expected $expected }
Reject 'Missing registry key' { Assert-RegistryShape $null $expected }
$wrong=[ordered]@{values=@([ordered]@{name='owner';kind='String';data='another'});children=@()}
Reject 'Registry value mismatch' { Assert-RegistryShape $wrong $expected }
$wrong=[ordered]@{values=@([ordered]@{name='owner';kind='ExpandString';data='run'});children=@()}
Reject 'Registry kind mismatch' { Assert-RegistryShape $wrong $expected }
$wrong=[ordered]@{values=@([ordered]@{name='owner';kind='String';data='run'},
    [ordered]@{name='extra';kind='String';data='unknown'});children=@()}
Reject 'Unexpected registry value' { Assert-RegistryShape $wrong $expected }
$wrong=[ordered]@{values=$expected.values;children=@([ordered]@{name='unexpected';shape=$expected})}
Reject 'Unexpected registry subkey' { Assert-RegistryShape $wrong $expected }
# Registration, cleanup and boot verification must agree on the one opt-in
# listener flag; all of these checks use literals or in-memory key shapes.
$script:receipt=$valid
$classId=$valid.classId
$listener=Expected-ListenerShape
if($listener.values.Count -ne 3 -or $listener.values[0].name -cne 'fEnableWinStation' -or
    $listener.values[0].kind -cne 'DWord' -or $listener.values[0].data -ne 1) {throw 'Listener ownership shape must include exactly the enabled DWORD.'}
Pass 'Owned listener shape includes fEnableWinStation DWORD1'
$writes=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.InvokeMemberExpressionAst] -and
    $node.Member -is [Management.Automation.Language.StringConstantExpressionAst] -and
    $node.Member.Value -ceq 'SetValue' -and $node.Arguments.Count -eq 3 -and
    $node.Arguments[0] -is [Management.Automation.Language.StringConstantExpressionAst] -and
    $node.Arguments[0].Value -ceq 'fEnableWinStation'
},$true))
if($writes.Count -ne 1 -or $writes[0].Arguments[1].Extent.Text -cne '1' -or
    $writes[0].Arguments[2].Extent.Text -cne '[Microsoft.Win32.RegistryValueKind]::DWord') {throw 'Listener enable write must be one literal DWORD1.'}
Pass 'Only one explicit enabled-listener write'
$bootTokens=$null;$bootErrors=$null
$bootAst=[Management.Automation.Language.Parser]::ParseFile(
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'lab\guest-provider-boot-trace.ps1'),[ref]$bootTokens,[ref]$bootErrors)
if($bootErrors.Count) {throw 'Boot helper syntax failed.'}
$registration=@($bootAst.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Assert-ActivationRegistration'
},$true))
if($registration.Count -ne 1) {throw 'Expected one boot registration validator.'}
. ([scriptblock]::Create($registration[0].Extent.Text))
$activation=$valid
$dll='C:\RETSAR-Lab\provider-activation\RETSAR-RdsBridge.dll'
$mockClass=[ordered]@{values=@([ordered]@{name='RETSARRunId';kind='String';data=$valid.runId});
    children=@([ordered]@{name='InprocServer32';shape=[ordered]@{values=@(
        [ordered]@{name='';kind='String';data=$dll},
        [ordered]@{name='ThreadingModel';kind='String';data='Free'});children=@()}})}
$script:mockListener=$listener
function Get-BootKeyShape([string]$Path) {
    if($Path -ceq ('SOFTWARE\Classes\CLSID\'+$classId)) {return $mockClass}
    if($Path -ceq 'SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RETSAR-Lab') {return $script:mockListener}
    throw 'Unexpected mock registry path.'
}
Assert-ActivationRegistration
Pass 'Boot helper accepts the same enabled-listener ownership shape'
$script:mockListener=$listener|ConvertTo-Json -Depth 10|ConvertFrom-Json
$script:mockListener.values[0].data=0
Reject 'Boot helper rejects disabled listener' {Assert-ActivationRegistration}
$script:mockListener.values=@($script:mockListener.values|Where-Object {$_.name -cne 'fEnableWinStation'})
Reject 'Boot helper rejects missing listener enable flag' {Assert-ActivationRegistration}
Write-Output ('ACTIVATION TOTAL '+$script:checks+' passed, 0 failed; pure validators and synthetic buffers only, no WTS calls, guest, registry, service or ETW operations.')
