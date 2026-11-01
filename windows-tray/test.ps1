$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$outputDir = Join-Path $PSScriptRoot 'test-output'
New-Item -ItemType Directory -Force $outputDir | Out-Null
$testExe = Join-Path $outputDir 'ProtocolTests.exe'
$sources = @((Join-Path $PSScriptRoot 'BatteryProtocol.cs'), (Join-Path $PSScriptRoot 'tests\ProtocolTests.cs'))
& $compiler /nologo /target:exe "/out:$testExe" $sources
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed.' }
