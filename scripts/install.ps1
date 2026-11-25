param([string]$TrafficMonitorDirectory = 'C:\TrafficMonitor')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$target = [IO.Path]::GetFullPath($TrafficMonitorDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $target 'TrafficMonitor.exe'))) { throw 'TrafficMonitor.exe not found' }
$pluginDirectory = Join-Path $target 'plugins'
New-Item -ItemType Directory -Force $pluginDirectory | Out-Null
foreach ($name in @('VgnVxeBattery.dll','BatteryCollector.exe')) {
    $source = Join-Path $repo "build\Release\$name"
    if (-not (Test-Path -LiteralPath $source)) { throw "Build output missing: $source" }
    $destination = Join-Path $pluginDirectory $name
    if (Test-Path -LiteralPath $destination) { Copy-Item -LiteralPath $destination -Destination "$destination.backup-$(Get-Date -Format yyyyMMdd-HHmmss)" }
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Write-Output 'Files installed. Restart TrafficMonitor and enable the two new display items.'
