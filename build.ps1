param([switch]$Test,[switch]$Lab,[switch]$RdsLab,[switch]$RdsTests,[switch]$ChildSessionLab)
$ErrorActionPreference='Stop'
if($RdsTests -and !$RdsLab){throw '-RdsTests requires -RdsLab.'}
$base=$PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Visual Studio Build Tools with C++ and the Windows SDK are required.'}
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'C++ build tools were not found.'}
$vcInstall=Get-ChildItem -LiteralPath "$vs\VC\Tools\MSVC" -Directory |
 Where-Object {$_.Name -match '^\d+(\.\d+)+$' -and
  (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe')) -and
  (Test-Path -LiteralPath (Join-Path $_.FullName 'include\vcruntime.h')) -and
  (Test-Path -LiteralPath (Join-Path $_.FullName 'lib\x64\libcmt.lib'))} |
 Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if(!$vcInstall){throw 'A complete x64 C++ compiler installation was not found.'}
$vc=$vcInstall.FullName
$sdk='C:\Program Files (x86)\Windows Kits\10'
# An Include directory may belong to a partial SDK installation. Select a
# version only when its compiler headers, x64 libraries and manifest tool exist.
$sdkInstall=Get-ChildItem -LiteralPath "$sdk\Include" -Directory |
 Where-Object {
  $candidateVersion=$_.Name
  if($candidateVersion -notmatch '^\d+(\.\d+){3}$'){return $false}
  $required=@("Include\$candidateVersion\ucrt\crtdbg.h","Include\$candidateVersion\um\Windows.h",
   "Include\$candidateVersion\shared\sdkddkver.h","Lib\$candidateVersion\ucrt\x64\ucrt.lib",
   "Lib\$candidateVersion\um\x64\kernel32.lib","bin\$candidateVersion\x64\mt.exe")
  @($required | Where-Object {!(Test-Path -LiteralPath (Join-Path $sdk $_) -PathType Leaf)}).Count -eq 0
 } | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if(!$sdkInstall){throw 'A complete Windows SDK with UCRT headers, x64 libraries and mt.exe is required.'}
$ver=$sdkInstall.Name
Write-Output "Toolchain: MSVC $($vcInstall.Name); Windows SDK $ver; x64."
$inc=@("$vc\include","$sdk\Include\$ver\ucrt","$sdk\Include\$ver\um","$sdk\Include\$ver\shared") | ForEach-Object { '/I'+$_ }
$libs=@("$vc\lib\x64","$sdk\Lib\$ver\ucrt\x64","$sdk\Lib\$ver\um\x64") | ForEach-Object { '/LIBPATH:'+$_ }
New-Item -ItemType Directory -Force -Path "$base\build","$base\dist","$base\reports" | Out-Null
$compilerPathBefore=$env:PATH
$env:PATH="$vc\bin\Hostx64\x64;$sdk\bin\$ver\x64;$env:PATH"
Push-Location -LiteralPath "$base\build"
try {
 & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\src\main.cpp" "$base\src\windows_inventory.cpp" "$base\src\lifecycle.cpp" "$base\src\journal.cpp" "/Fe:$base\dist\RETSAR.exe" /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED "/MANIFESTINPUT:$base\src\app.manifest" @libs user32.lib wtsapi32.lib bcrypt.lib
 if($LASTEXITCODE -ne 0){throw 'RETSAR compilation failed.'}
 if($Lab){
  New-Item -ItemType Directory -Force -Path "$base\dist\lab" | Out-Null
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\src\lab\seat_probe.cpp" "/Fe:$base\dist\lab\RETSAR-seat-probe.exe" /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED "/MANIFESTINPUT:$base\src\app.manifest" @libs user32.lib gdi32.lib advapi32.lib
  if($LASTEXITCODE -ne 0){throw 'Laboratory probe compilation failed.'}
  # Help is intentionally the only probe mode executed by the build.
  & "$base\dist\lab\RETSAR-seat-probe.exe" --help
  if($LASTEXITCODE -ne 0){throw 'The inert laboratory probe help check failed.'}
 }
 if($ChildSessionLab){
  New-Item -ItemType Directory -Force -Path "$base\dist\lab" | Out-Null
  foreach($probe in @(@{source='child_session_probe';name='child-session-probe'},@{source='child_session_toggle_probe';name='child-session-toggle'})){
   & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\src\lab\$($probe.source).cpp" "/Fe:$base\dist\lab\RETSAR-$($probe.name).exe" /link /SUBSYSTEM:CONSOLE @libs wtsapi32.lib
   if($LASTEXITCODE -ne 0){throw 'Child-session laboratory diagnostic compilation failed.'}
   # Help only: never query or change session state as part of a build.
   & "$base\dist\lab\RETSAR-$($probe.name).exe" --help
   if($LASTEXITCODE -ne 0){throw 'Inert child-session help check failed.'}
  }
 }
 if($Test -or $ChildSessionLab){
  foreach($contract in @('child_session_status','child_session_toggle')){
   & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\${contract}_tests.cpp" "/Fe:$base\build\RETSAR-${contract}-tests.exe" /link /SUBSYSTEM:CONSOLE @libs wtsapi32.lib
   if($LASTEXITCODE -ne 0){throw "Child-session pure test compilation failed: $contract"}
   & "$base\build\RETSAR-${contract}-tests.exe"
   if($LASTEXITCODE -ne 0){throw "Child-session pure tests failed: $contract"}
  }
 }
 if($RdsLab){
  New-Item -ItemType Directory -Force -Path "$base\dist\lab" | Out-Null
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT /LD @inc "$base\src\rds\native_rds_bridge.cpp" "$base\src\rds\lab_control_channel.cpp" "$base\src\rds\lab_control_worker.cpp" "/Fe:$base\dist\lab\RETSAR-RdsBridge.dll" /link "/DEF:$base\src\rds\native_rds_bridge.def" @libs ole32.lib advapi32.lib
  if($LASTEXITCODE -ne 0){throw 'The optional RDS laboratory bridge compilation failed.'}
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\src\rds\lab_control_client.cpp" "$base\src\rds\lab_control_channel.cpp" "/Fe:$base\dist\lab\RETSAR-rds-control.exe" /link /SUBSYSTEM:CONSOLE @libs ole32.lib advapi32.lib
  if($LASTEXITCODE -ne 0){throw 'The owned-guest RDS controller compilation failed.'}
  & "$base\dist\lab\RETSAR-rds-control.exe" --help
  if($LASTEXITCODE -ne 0){throw 'Inert controller help check failed.'}
  if($Test -or $RdsTests){
   foreach($contract in @('license_transport','remote_display_contract','lab_control_channel')){
    & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\${contract}_tests.cpp" "$base\src\rds\${contract}.cpp" "/Fe:$base\build\RETSAR-${contract}-tests.exe" /link /SUBSYSTEM:CONSOLE @libs ole32.lib advapi32.lib
    if($LASTEXITCODE -ne 0){throw "Contract test compilation failed: $contract"}
    & "$base\build\RETSAR-${contract}-tests.exe"
    if($LASTEXITCODE -ne 0){throw "Inert contract tests failed: $contract"}
   }
   & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DRETSAR_WORKER_TEST_DLL /O2 /MT /LD @inc "$base\tests\lab_control_worker_tests.cpp" "$base\src\rds\lab_control_worker.cpp" "/Fe:$base\build\RETSAR-worker-fixture.dll" /link @libs ole32.lib
   if($LASTEXITCODE -ne 0){throw 'The MOCK worker fixture compilation failed.'}
   & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\lab_control_worker_tests.cpp" "/Fe:$base\build\RETSAR-worker-tests.exe" /link /SUBSYSTEM:CONSOLE @libs ole32.lib
   if($LASTEXITCODE -ne 0){throw 'The MOCK worker harness compilation failed.'}
   & "$base\build\RETSAR-worker-tests.exe" "$base\build\RETSAR-worker-fixture.dll"
   if($LASTEXITCODE -ne 0){throw 'The MOCK worker tests failed.'}
   & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\rds_bridge_tests.cpp" "/Fe:$base\build\RETSAR-rds-bridge-tests.exe" /link /SUBSYSTEM:CONSOLE @libs ole32.lib
   if($LASTEXITCODE -ne 0){throw 'The in-process MOCK RDS harness compilation failed.'}
   & "$base\build\RETSAR-rds-bridge-tests.exe" "$base\dist\lab\RETSAR-RdsBridge.dll"
   if($LASTEXITCODE -ne 0){throw 'The in-process MOCK RDS contract tests failed.'}
  }
 }
 if($Test){
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\probe_args_tests.cpp" "/Fe:$base\build\RETSAR-probe-args-tests.exe" /link /SUBSYSTEM:CONSOLE @libs
  if($LASTEXITCODE -ne 0){throw 'Laboratory argument test compilation failed.'}
  & "$base\build\RETSAR-probe-args-tests.exe"
  if($LASTEXITCODE -ne 0){throw 'Laboratory argument tests failed.'}
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\main.cpp" "$base\src\windows_inventory.cpp" "/Fe:$base\build\RETSAR-tests.exe" /link /SUBSYSTEM:CONSOLE @libs user32.lib wtsapi32.lib
  if($LASTEXITCODE -ne 0){throw 'Model test compilation failed.'}
  & "$base\build\RETSAR-tests.exe"
  if($LASTEXITCODE -ne 0){throw 'Model tests failed.'}
  $fixtureJson=& "$base\build\RETSAR-tests.exe" --json-fixture
  if($LASTEXITCODE -ne 0){throw 'The JSON test fixture could not be generated.'}
  $fixture=$fixtureJson | ConvertFrom-Json
  if($fixture.schema -ne 'retsar.test-fixture.v1' -or !$fixture.testOnly -or $fixture.capabilities.independentInput -ne 'NOT_IMPLEMENTED'){throw 'Independent JSON validation failed.'}
  [System.IO.File]::WriteAllText("$base\build\json-fixture.json",$fixtureJson,[System.Text.UTF8Encoding]::new($false))
  Write-Output 'PASS JSON parsed independently by PowerShell ConvertFrom-Json (test fixture; no inventory).'
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\lifecycle_tests.cpp" "$base\src\lifecycle.cpp" "$base\src\journal.cpp" "/Fe:$base\build\RETSAR-lifecycle-tests.exe" /link /SUBSYSTEM:CONSOLE @libs user32.lib bcrypt.lib
  if($LASTEXITCODE -ne 0){throw 'Lifecycle test compilation failed.'}
  & "$base\build\RETSAR-lifecycle-tests.exe" "$base\build"
  if($LASTEXITCODE -ne 0){throw 'Lifecycle tests failed.'}
  & "$base\tests\cli_tests.ps1"
 }
} finally {Pop-Location;$env:PATH=$compilerPathBefore}
