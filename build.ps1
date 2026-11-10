param([switch]$Test)
$ErrorActionPreference='Stop'
$base=$PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere)){throw 'Se necesitan Visual Studio Build Tools con C++ y Windows SDK.'}
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'No se encontraron las herramientas de C++.'}
$vc=(Get-ChildItem -LiteralPath "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk='C:\Program Files (x86)\Windows Kits\10'
$ver=(Get-ChildItem -LiteralPath "$sdk\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$inc=@("$vc\include","$sdk\Include\$ver\ucrt","$sdk\Include\$ver\um","$sdk\Include\$ver\shared") | ForEach-Object { '/I'+$_ }
$libs=@("$vc\lib\x64","$sdk\Lib\$ver\ucrt\x64","$sdk\Lib\$ver\um\x64") | ForEach-Object { '/LIBPATH:'+$_ }
New-Item -ItemType Directory -Force -Path "$base\build","$base\dist","$base\reports" | Out-Null
$compilerPathBefore=$env:PATH
$env:PATH="$vc\bin\Hostx64\x64;$sdk\bin\$ver\x64;$env:PATH"
Push-Location -LiteralPath "$base\build"
try {
 & "$vc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /O2 /MT @inc "$base\src\main.cpp" "$base\src\windows_inventory.cpp" "$base\src\lifecycle.cpp" "$base\src\journal.cpp" "/Fe:$base\dist\RETSAR.exe" /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED "/MANIFESTINPUT:$base\src\app.manifest" @libs user32.lib wtsapi32.lib bcrypt.lib
 if($LASTEXITCODE -ne 0){throw 'Fallo al compilar RETSAR.'}
 if($Test){
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
