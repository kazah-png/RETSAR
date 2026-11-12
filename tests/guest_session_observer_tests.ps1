#requires -Version 5.1
# Inert checks only: parse the guest script and compile its C# declarations.
# Never dot-source the observer, invoke Capture(), or call a WTS entry point.
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitProcess) { throw 'The observer layout checks require x64 PowerShell.' }

$base = Split-Path -Parent $PSScriptRoot
$observerPath = Join-Path $base 'lab\guest-session-observer.ps1'
$parseTokens = $null
$parseErrors = $null
$observerAst = [Management.Automation.Language.Parser]::ParseFile(
    $observerPath, [ref]$parseTokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ('Observer syntax failed: ' + ($parseErrors.Message -join '; ')) }
$nativeLiterals = @($observerAst.FindAll({
    param($node)
    $node -is [Management.Automation.Language.StringConstantExpressionAst] -and
        $node.Value.StartsWith('using System;')
}, $true))
if ($nativeLiterals.Count -ne 1) { throw 'Expected exactly one native declaration literal.' }
Add-Type -TypeDefinition $nativeLiterals[0].Value -ErrorAction Stop

# ABI prerequisites are checked separately from the 24 + 3 regression checks.
$layout = @(
    [Runtime.InteropServices.Marshal]::SizeOf([type][Retsar.GuestObservation.SessionEntry]),
    [Runtime.InteropServices.Marshal]::SizeOf([type][Retsar.GuestObservation.InfoLevel1]),
    [Runtime.InteropServices.Marshal]::SizeOf([type][Retsar.GuestObservation.InfoEx]),
    [Runtime.InteropServices.Marshal]::OffsetOf([type][Retsar.GuestObservation.InfoEx], 'Data').ToInt32(),
    [Runtime.InteropServices.Marshal]::OffsetOf([type][Retsar.GuestObservation.InfoLevel1], 'LogonTime').ToInt32()
)
if (($layout -join ',') -ne '24,224,232,8,160') { throw 'Unexpected native structure layout.' }
Write-Output 'PASS OBSERVER prerequisites: parsed, compiled and checked x64 layout; no guest-script execution.'

$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw ('FAIL OBSERVER: ' + $Name) }
    $checks.Add($Name)
    Write-Output ('PASS OBSERVER: ' + $Name)
}

# Only WTSListen (6) is non-applicable, including when future/invalid states appear.
foreach ($state in -1..10) {
    $row = [Retsar.GuestObservation.Reader]::ClassifyEntry(42, $state)
    $listener = $state -eq 6
    $kind = if ($listener) { 'listener' } else { 'session' }
    $queryStatus = if ($listener) { 'not-applicable-listener' } else { 'not-queried' }
    Check ($row.kind -eq $kind -and $row.identityQueryStatus -eq $queryStatus -and
        !$row.queryAttempted -and !$row.querySucceeded -and $row.sessionId -eq 42 -and
        $row.enumerationState -eq $state) "Classification for state $state"
}
foreach ($item in @(@{id=65536;state=0}, @{id=65537;state=4})) {
    $row = [Retsar.GuestObservation.Reader]::ClassifyEntry($item.id, $item.state)
    Check ($row.kind -eq 'session' -and $row.identityQueryStatus -eq 'not-queried') (
        'High ID does not classify a listener: ' + $item.id)
}
foreach ($listenerId in @(0, 65536, 65537)) {
    $row = [Retsar.GuestObservation.Reader]::ClassifyEntry($listenerId, 6)
    Check ($row.kind -eq 'listener' -and !$row.queryAttempted -and !$row.querySucceeded -and
        $row.identityQueryStatus -eq 'not-applicable-listener' -and $null -eq $row.user -and
        $null -eq $row.logonTime) "Explicit listener without logon identity: $listenerId"
}

$valid = [Retsar.GuestObservation.Reader]::ValidateInfoHeader(7, 1, 7, 232)
Check ($null -eq $valid) 'Matching level-one header accepted'
$mismatch = [Retsar.GuestObservation.Reader]::ValidateInfoHeader(7, 1, 8, 232)
Check ($mismatch.sessionId -eq 7 -and $mismatch.returnedLevel -eq 1 -and
    $mismatch.returnedSessionId -eq 8 -and $mismatch.returnedBytes -eq 232 -and
    $mismatch.stage -eq 'query-level-or-id-mismatch') 'Mismatched ID diagnostics preserved'
$unknownLevel = [Retsar.GuestObservation.Reader]::ValidateInfoHeader(7, 2, 8, 232)
Check ($null -ne $unknownLevel -and $unknownLevel.returnedLevel -eq 2 -and
    $null -eq $unknownLevel.returnedSessionId) 'Unknown level does not expose a level-one ID'
$missingId = [Retsar.GuestObservation.Reader]::ValidateInfoHeader(7, 1, $null, 232)
Check ($null -ne $missingId -and $null -eq $missingId.returnedSessionId) 'Missing ID remains an error'

$snapshot = [Retsar.GuestObservation.Snapshot]::new()
$snapshot.evidence.Add([Retsar.GuestObservation.Reader]::ClassifyEntry(65536, 6))
Check (!$snapshot.incomplete) 'Listener alone does not make evidence incomplete'
$snapshot.errors.Add($mismatch)
Check ($snapshot.incomplete) 'Applicable-session mismatch keeps evidence incomplete'
$snapshot.errors.Clear()
$snapshot.truncated = $true
Check ($snapshot.incomplete) 'Truncated listener enumeration keeps evidence incomplete'
if ($checks.Count -ne 24) { throw 'Unexpected classification/diagnostic check count.' }

$zeroTime = [Retsar.GuestObservation.TimeValue]::new(0)
Check ($zeroTime.status -eq 'unavailable' -and $null -eq $zeroTime.utc) 'Zero timestamp is unavailable'
$invalidTime = [Retsar.GuestObservation.TimeValue]::new([long]::MaxValue)
Check ($invalidTime.status -eq 'invalid' -and $null -eq $invalidTime.utc) 'Out-of-range timestamp is invalid'
$validRaw = [long]::Parse('132537600000000001', [Globalization.CultureInfo]::InvariantCulture)
$validTime = [Retsar.GuestObservation.TimeValue]::new($validRaw)
$roundTrip = [DateTime]::Parse($validTime.utc, [Globalization.CultureInfo]::InvariantCulture,
    [Globalization.DateTimeStyles]::RoundtripKind).ToFileTimeUtc()
Check ($validTime.status -eq 'valid' -and $roundTrip -eq $validRaw -and
    $validTime.raw100ns -eq $validRaw.ToString([Globalization.CultureInfo]::InvariantCulture)) (
    'Timestamp preserves FILETIME precision through UTC round trip')
if ($checks.Count -ne 27) { throw 'Unexpected observer check count.' }

Write-Output 'OBSERVER TOTAL 27 passed, 0 failed; 24 classification/diagnostic and 3 timestamp checks; no WTS calls.'
