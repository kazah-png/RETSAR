#requires -Version 5.1
# Inert tests: pure validators, layout and synthetic buffers. No registry,
# ControlTrace, service operations, reboot, guest access or provider loading.
$ErrorActionPreference='Stop'
$path=Join-Path (Split-Path -Parent $PSScriptRoot) 'lab\guest-provider-boot-trace.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors.Message -join '; ')}
foreach($name in @('Assert-BootPath','Assert-BootReceipt','Get-ExpectedBootShape','Assert-BootShape','Get-OwnedBootStart')) {
    $nodes=@($ast.FindAll({param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name
    },$true))
    if($nodes.Count -ne 1){throw ('Missing or duplicate pure helper '+$name)}
    . ([scriptblock]::Create($nodes[0].Extent.Text))
}
$script:count=0
function Pass([string]$Name){$script:count++;Write-Output ('PASS BOOT TRACE: '+$Name)}
function Check([bool]$Condition,[string]$Name){if(!$Condition){throw ('FAIL BOOT TRACE: '+$Name)};Pass $Name}
function Reject([string]$Name,[scriptblock]$Block){
    $threw=$false
    try{& $Block}catch{$threw=$true}
    Check $threw $Name
}
$valid=[pscustomobject][ordered]@{
    schema='retsar.provider-boot-trace.v1';runId='0123456789abcdef0123456789abcdef';
    traceName='RETSAR-ActivationBoot-0123456789abcdef0123456789abcdef';
    traceGuid='{12345678-9ABC-4DEF-8123-456789ABCDEF}';activationReceiptSha256=('A'*64);
    dllSha256=('B'*64);providerGuid='{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}';
    armed=$true;disarmed=$false;traceStopped=$false;registryRemoved=$false
}
Assert-BootReceipt $valid;Pass 'Exact boot ownership receipt'
foreach($change in @(
    @('schema','wrong'),@('runId','not-a-guid'),@('traceName','other'),
    @('traceGuid','{00000000-0000-0000-0000-000000000000}'),
    @('traceGuid','{12345678-9abc-4def-8123-456789abcdef}'),
    @('traceGuid','{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}'),
    @('activationReceiptSha256',('C'*63)),@('dllSha256',('g'*64)),
    @('providerGuid','{00000000-0000-0000-0000-000000000000}'),
    @('armed','true'),@('disarmed',1),@('traceStopped',$null),@('registryRemoved','false')
)){
    $bad=$valid|ConvertTo-Json|ConvertFrom-Json
    $bad.($change[0])=$change[1]
    Reject ('Receipt rejects altered '+$change[0]) {Assert-BootReceipt $bad}
}
Assert-BootPath 'C:\RETSAR-Lab\provider-activation\bootactivation.etl';Pass 'New boot ETL path'
foreach($badPath in @('E:\anything','D:\RETSAR-Lab\provider-activation',
    'C:\RETSAR-Lab\provider-activation\activation.etl','C:\RETSAR-Lab\provider-activation\..\receipt.json',
    'C:\RETSAR-Lab\provider-activation\bootactivation.etl:stream','\\server\share')){
    Reject ('Path rejected before lookup '+$badPath){Assert-BootPath $badPath}
}
$shape=Get-ExpectedBootShape $valid.traceGuid 1
Assert-BootShape $shape $shape;Pass 'Exact AutoLogger configuration'
Check ($shape.values.Count -eq 9 -and $shape.children.Count -eq 1 -and
    $shape.children[0].shape.values.Count -eq 3) 'Only one bounded provider configuration'
Check (@($shape.values|Where-Object {$_.name -in @('Boot','FileMax','FileCounter')}).Count -eq 0) 'No early-kernel mode or rotating overwrite configuration'
$withStatus=$shape|ConvertTo-Json -Depth 10|ConvertFrom-Json
$withStatus.values+= [pscustomobject]@{name='Status';kind='DWord';data=[int]0}
Assert-BootShape $withStatus $shape;Pass 'Windows success status is tolerated'
$withStatus.values[-1].data=[int]5
Assert-BootShape $withStatus $shape;Pass 'Windows startup error status remains valid evidence'
$withStatus.values[-1].kind='String'
Reject 'Unexpected status value kind'{Assert-BootShape $withStatus $shape}
$providerStatusShape=$shape|ConvertTo-Json -Depth 10|ConvertFrom-Json
$providerStatusShape.values+=[pscustomobject]@{name='Status';kind='DWord';data=[int]0}
$providerStatusShape.children[0].shape.values+=[pscustomobject]@{name='Status';kind='DWord';data=[int]0}
Assert-BootShape $providerStatusShape $shape;Pass 'Windows root and provider success statuses are tolerated'
$providerStatusShape.children[0].shape.values[-1].data=[int]5
Assert-BootShape $providerStatusShape $shape;Pass 'Provider startup error remains valid evidence'
$providerStatusShape.children[0].shape.values[-1].kind='String'
Reject 'Provider status wrong registry kind'{Assert-BootShape $providerStatusShape $shape}
$providerStatusShape.children[0].shape.values[-1].kind='DWord'
$providerStatusShape.children[0].shape.values[-1].data='0'
Reject 'Provider status wrong value type'{Assert-BootShape $providerStatusShape $shape}
$providerStatusShape.children[0].shape.values[-1].data=[int]0
$providerStatusShape.children[0].shape.values+=$providerStatusShape.children[0].shape.values[-1]
Reject 'Duplicate provider status'{Assert-BootShape $providerStatusShape $shape}
$providerStatusShape=$shape|ConvertTo-Json -Depth 10|ConvertFrom-Json
$providerStatusShape.children[0].shape.values+=[pscustomobject]@{name='Unexpected';kind='DWord';data=[int]0}
Reject 'Unrelated provider value is not normalized away'{Assert-BootShape $providerStatusShape $shape}
$extra=$shape|ConvertTo-Json -Depth 10|ConvertFrom-Json
$extra.values+=[pscustomobject]@{name='Boot';kind='DWord';data=1}
Reject 'Additional AutoLogger setting'{Assert-BootShape $extra $shape}
$extra=$shape|ConvertTo-Json -Depth 10|ConvertFrom-Json
$extra.children+=$extra.children[0]
Reject 'Additional provider'{Assert-BootShape $extra $shape}
$extra=$shape|ConvertTo-Json -Depth 10|ConvertFrom-Json
($extra.values|Where-Object {$_.name -eq 'FileName'}).data='C:\different.etl'
Reject 'Changed trace output path'{Assert-BootShape $extra $shape}
Reject 'Changed Start state'{Assert-BootShape (Get-ExpectedBootShape $valid.traceGuid 0) $shape}
Reject 'Invalid Start argument'{Get-ExpectedBootShape $valid.traceGuid 2}
Check ((Get-OwnedBootStart (Get-ExpectedBootShape $valid.traceGuid 0) $valid) -eq 0) 'Interrupted Arm or disarm remains recoverable'
$disarmed=$valid|ConvertTo-Json|ConvertFrom-Json
$disarmed.disarmed=$true
Reject 'Outside rearming after committed disarm'{Get-OwnedBootStart $shape $disarmed}
$source=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.StringConstantExpressionAst] -and
    $node.Value -like '*public static class RetsarBootTraceNative*'
},$true))
if($source.Count -ne 1){throw 'Missing native declaration block'}
Add-Type -TypeDefinition $source[0].Value
Check ([Runtime.InteropServices.Marshal]::SizeOf([type][RetsarBootTraceNative+Wnode]) -eq 48) 'WNODE_HEADER x64 layout is 48 bytes'
Check ([Runtime.InteropServices.Marshal]::SizeOf([type][RetsarBootTraceNative+Properties]) -eq 120) 'EVENT_TRACE_PROPERTIES x64 layout is 120 bytes'
Check ([Runtime.InteropServices.Marshal]::OffsetOf([type][RetsarBootTraceNative+Properties],'LoggerNameOffset').ToInt32() -eq 116) 'Logger name offset field is correctly aligned'
$info=[RetsarBootTraceNative+TraceInfo]::new()
$info.Running=$true;$info.Result=0;$info.Guid=[guid]$valid.traceGuid;$info.Name=$valid.traceName
$info.FileName='C:\RETSAR-Lab\provider-activation\bootactivation.etl';$info.Mode=2;$info.MaximumFileSize=16
Check ([RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Exact running trace identity'
Check (![RetsarBootTraceNative]::IsOwned($info,'other',$info.FileName,[guid]$valid.traceGuid)) 'Wrong trace name rejected'
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,'C:\other.etl',[guid]$valid.traceGuid)) 'Wrong active output rejected'
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]::Empty)) 'Wrong active GUID rejected'
$info.Mode=0x00400002
Check ([RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Documented stop-on-hybrid-shutdown default accepted'
$info.Mode=0x00800002
Check ([RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Documented persist-on-hybrid-shutdown default accepted'
$info.Mode=0x00C00002
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Conflicting hybrid-shutdown modes rejected'
$info.Mode=0x00000102
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Unexpected real-time tracing mode rejected'
$info.Mode=0x00000042
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Nonstoppable tracing mode rejected'
$info.Mode=0x00800102
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Valid shutdown default cannot conceal unrelated mode flags'
$info.Mode=1
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Unexpected active mode rejected'
$info.Mode=2;$info.MaximumFileSize=0
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Unbounded active trace rejected'
$info.MaximumFileSize=16;$info.Running=$false
Check (![RetsarBootTraceNative]::IsOwned($info,$valid.traceName,$info.FileName,[guid]$valid.traceGuid)) 'Inactive trace cannot authorize a stop'
$capacity=4216;$buffer=[Runtime.InteropServices.Marshal]::AllocHGlobal($capacity)
try {
    [Runtime.InteropServices.Marshal]::Copy([byte[]]::new($capacity),0,$buffer,$capacity)
    Check ([RetsarBootTraceNative]::ReadBoundedString($buffer,$capacity,120) -ceq '') 'Empty terminated text buffer'
    [Runtime.InteropServices.Marshal]::WriteInt16($buffer,120,65)
    Check ([RetsarBootTraceNative]::ReadBoundedString($buffer,$capacity,120) -ceq 'A') 'Bounded UTF16 decoder'
    foreach($offset in @(0,118,121,4216,5000)){
        Reject ('Bad ETW text offset '+$offset){[RetsarBootTraceNative]::ReadBoundedString($buffer,$capacity,$offset)}
    }
    for($i=120;$i -lt 2168;$i+=2){[Runtime.InteropServices.Marshal]::WriteInt16($buffer,$i,65)}
    Reject 'Unterminated maximum-length ETW text'{[RetsarBootTraceNative]::ReadBoundedString($buffer,$capacity,120)}
    Reject 'Null ETW text buffer'{[RetsarBootTraceNative]::ReadBoundedString([IntPtr]::Zero,$capacity,120)}
} finally {[Runtime.InteropServices.Marshal]::FreeHGlobal($buffer)}
$forbidden=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -in @('Restart-Computer','Restart-Service','Stop-Service','Start-Service','New-LocalUser','Remove-LocalUser')
},$true))
Check ($forbidden.Count -eq 0) 'Boot helper contains no reboot, service or account commands'
Write-Output ('BOOT TRACE TOTAL '+$script:count+' passed, 0 failed; pure helpers, layout and synthetic buffers only.')
