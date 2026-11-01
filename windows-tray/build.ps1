$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.x C# derleyicisi bulunamadı.' }
$destination = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $destination | Out-Null
$sources = @('Program.cs', 'SerialMonitor.cs', 'BatteryProtocol.cs', 'BatteryDashboard.cs') | ForEach-Object { Join-Path $PSScriptRoot $_ }
$outputFile = Join-Path $destination 'USBAirPods.exe'
$appIcon = Join-Path $PSScriptRoot 'USBAirPods.ico'
& $compiler /nologo /target:winexe /platform:anycpu /optimize+ /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll "/win32icon:$appIcon" "/out:$outputFile" $sources
if ($LASTEXITCODE -ne 0) { throw 'Derleme başarısız.' }
Write-Output "Hazır: $destination/USBAirPods.exe"
