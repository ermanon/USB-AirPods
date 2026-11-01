$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual Studio C++ Build Tools is required.' }
$msvc = Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem (Join-Path $kits 'Lib') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$previousInclude = $env:INCLUDE
$previousLib = $env:LIB
try {
    $env:INCLUDE = "$($msvc.FullName)\include;$kits\Include\$($sdk.Name)\ucrt;$kits\Include\$($sdk.Name)\shared;$kits\Include\$($sdk.Name)\um"
    $env:LIB = "$($msvc.FullName)\lib\x64;$kits\Lib\$($sdk.Name)\ucrt\x64;$kits\Lib\$($sdk.Name)\um\x64"
    $outputDir = Join-Path $PSScriptRoot '..\windows-tray\test-output'
    New-Item -ItemType Directory -Force $outputDir | Out-Null
    $testExe = Join-Path $outputDir 'battery_test.exe'
    $testObj = Join-Path $outputDir 'battery_test.obj'
    $testSource = Join-Path $PSScriptRoot 'battery_test.c'
    & (Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe') /nologo /W4 /TC /std:c11 $testSource "/Fo:$testObj" "/Fe:$testExe"
    if ($LASTEXITCODE -ne 0) { throw 'Firmware parser test compilation failed.' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Firmware parser test failed.' }
} finally {
    $env:INCLUDE = $previousInclude
    $env:LIB = $previousLib
}
