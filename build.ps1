param([switch]$Test,[switch]$Lab)
$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Se necesitan Visual Studio Build Tools con C++ y Windows SDK.'}
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'No se encontraron las herramientas de C++.'}
$vcInstall=Get-ChildItem -LiteralPath "$vs\VC\Tools\MSVC" -Directory |
 Where-Object {$_.Name -match '^\d+(\.\d+)+$' -and
  (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe')) -and
  (Test-Path -LiteralPath (Join-Path $_.FullName 'include\vcruntime.h')) -and
  (Test-Path -LiteralPath (Join-Path $_.FullName 'lib\x64\libcmt.lib'))} |
 Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
if(!$vcInstall){throw 'No hay una instalacion x64 completa del compilador C++.'}
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
if(!$sdkInstall){throw 'Falta un Windows SDK completo con UCRT, cabeceras, bibliotecas x64 y mt.exe.'}
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
 if($LASTEXITCODE -ne 0){throw 'Fallo al compilar RETSAR.'}
 if($Lab){
  New-Item -ItemType Directory -Force -Path "$base\dist\lab" | Out-Null
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\src\lab\seat_probe.cpp" "/Fe:$base\dist\lab\RETSAR-seat-probe.exe" /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED "/MANIFESTINPUT:$base\src\app.manifest" @libs user32.lib gdi32.lib
  if($LASTEXITCODE -ne 0){throw 'Fallo al compilar el instrumento de laboratorio.'}
  # Help is intentionally the only probe mode executed by the build.
  & "$base\dist\lab\RETSAR-seat-probe.exe" --help
  if($LASTEXITCODE -ne 0){throw 'Fallo en la ayuda sin observacion del instrumento.'}
 }
 if($Test){
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\probe_args_tests.cpp" "/Fe:$base\build\RETSAR-probe-args-tests.exe" /link /SUBSYSTEM:CONSOLE @libs
  if($LASTEXITCODE -ne 0){throw 'Fallo al compilar las pruebas puras del instrumento.'}
  & "$base\build\RETSAR-probe-args-tests.exe"
  if($LASTEXITCODE -ne 0){throw 'Fallaron las pruebas puras del instrumento.'}
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\main.cpp" "$base\src\windows_inventory.cpp" "/Fe:$base\build\RETSAR-tests.exe" /link /SUBSYSTEM:CONSOLE @libs user32.lib wtsapi32.lib
  if($LASTEXITCODE -ne 0){throw 'Fallo al compilar las pruebas.'}
  & "$base\build\RETSAR-tests.exe"
  if($LASTEXITCODE -ne 0){throw 'Fallaron pruebas del modelo.'}
  $fixtureJson=& "$base\build\RETSAR-tests.exe" --json-fixture
  if($LASTEXITCODE -ne 0){throw 'No se genero el fixture JSON.'}
  $fixture=$fixtureJson | ConvertFrom-Json
  if($fixture.schema -ne 'retsar.test-fixture.v1' -or !$fixture.testOnly -or $fixture.capabilities.independentInput -ne 'NOT_IMPLEMENTED'){throw 'Validacion externa de JSON fallida.'}
  [System.IO.File]::WriteAllText("$base\build\json-fixture.json",$fixtureJson,[System.Text.UTF8Encoding]::new($false))
  Write-Output 'PASS JSON parsed independently by PowerShell ConvertFrom-Json (test fixture; no inventory).'
  & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\tests\lifecycle_tests.cpp" "$base\src\lifecycle.cpp" "$base\src\journal.cpp" "/Fe:$base\build\RETSAR-lifecycle-tests.exe" /link /SUBSYSTEM:CONSOLE @libs user32.lib bcrypt.lib
  if($LASTEXITCODE -ne 0){throw 'Fallo al compilar las pruebas del ciclo de vida.'}
  & "$base\build\RETSAR-lifecycle-tests.exe" "$base\build"
  if($LASTEXITCODE -ne 0){throw 'Fallaron pruebas del ciclo de vida.'}
  & "$base\tests\cli_tests.ps1"
 }
} finally {Pop-Location;$env:PATH=$compilerPathBefore}
