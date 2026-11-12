param([string]$TestOutputDirectory)
$ErrorActionPreference='Stop'
$base=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $base 'dist\RETSAR.exe'
if(!$TestOutputDirectory){$TestOutputDirectory=Join-Path $base 'build'}
$TestOutputDirectory=[IO.Path]::GetFullPath($TestOutputDirectory)
if([IO.Path]::GetPathRoot($TestOutputDirectory) -ine 'C:\'){throw 'CLI output fixtures require C:; use build.ps1 -Test from a checkout on another drive.'}
if(!(Test-Path -LiteralPath $TestOutputDirectory -PathType Container)){throw 'The test fixture directory must already exist.'}
New-Item -ItemType Directory -Force -Path (Join-Path $base 'build'),(Join-Path $base 'reports') | Out-Null
$checks=[System.Collections.Generic.List[object]]::new()
$stateText=& $exe --engine-status
if($LASTEXITCODE -ne 0){throw 'CLI status failed'}
$state=$stateText | ConvertFrom-Json
if($state.schema -ne 'retsar.engine-status.v1' -or $state.name -ne 'RETSAR' -or $state.displayName -ne 'Runtime Environment for Terminal Sharing and Allocation of Resources' -or $state.operationalMultiseat -or $state.hostInspected -or $state.journalOpened -or $state.windowsCapabilities.sessionLifecycle -ne 'NOT_IMPLEMENTED'){throw 'Unsafe or unexpected readiness report'}
$checks.Add([pscustomobject]@{check='Readiness JSON has native creation disabled; no host inspection or journal';result='PASS'})
$target=Join-Path $TestOutputDirectory ('cli-state-'+[guid]::NewGuid().ToString('N')+'.json')
$result=& $exe --engine-status --output $target
if($LASTEXITCODE -ne 0){throw ('CLI new C file failed: '+($result -join ' '))}
$disk=Get-Content -LiteralPath $target -Raw | ConvertFrom-Json
if($disk.schema -ne $state.schema){throw 'Output JSON mismatch'}
$checks.Add([pscustomobject]@{check='New ordinary C file round trip';result='PASS'})
$hash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
$refused=& $exe --engine-status --output $target
if($LASTEXITCODE -ne 1 -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $hash){throw 'Existing output was not protected'}
$checks.Add([pscustomobject]@{check='Existing output refused and contents unchanged';result='PASS'})
$bad=& $exe --engine-status --output (Join-Path $TestOutputDirectory 'NUL')
if($LASTEXITCODE -ne 1){throw 'Reserved DOS name was accepted'}
$checks.Add([pscustomobject]@{check='Reserved DOS target rejected';result='PASS'})
$unknown=& $exe --invalid-option
if($LASTEXITCODE -ne 1){throw 'Unknown option was accepted'}
$checks.Add([pscustomobject]@{check='Unknown option rejected without inventory';result='PASS'})
$help=& $exe --help
if($LASTEXITCODE -ne 0 -or ($help -join "`n") -notmatch 'RETSAR 0.2.0-engine-preview' -or ($help -join "`n") -notmatch 'Runtime Environment for Terminal Sharing and Allocation of Resources'){throw 'Help name or version mismatch'}
$checks.Add([pscustomobject]@{check='Help reports engine preview version';result='PASS'})
$report=[pscustomobject]@{schema='retsar.cli-validation.v1';version=$state.version;executableSHA256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;hostInspected=$false;checks=$checks;outputArtifact=$target}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $base 'reports\validation-cli.json') -Encoding utf8
$checks | ForEach-Object {Write-Output ('PASS CLI: '+$_.check)}
Write-Output ('CLI TOTAL '+$checks.Count+' passed, 0 failed; no host inventory or seat mutation.')
