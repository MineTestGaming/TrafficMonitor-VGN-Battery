param([string]$Generator = 'Visual Studio 18 2026')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    cmake -S . -B build -G $Generator -A x64
    if ($LASTEXITCODE) { throw 'CMake configure failed' }
    cmake --build build --config Release
    if ($LASTEXITCODE) { throw 'Build failed' }
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
    $package = Join-Path $repo 'dist\VgnVxeBattery-x64'
    New-Item -ItemType Directory -Force $package, "$package\docs", "$package\third_party" | Out-Null
    Copy-Item -LiteralPath 'build\Release\VgnVxeBattery.dll','build\Release\BatteryCollector.exe','README.md','LICENSE' -Destination $package -Force
    Copy-Item -LiteralPath 'docs\protocol-analysis.md','docs\validation.md' -Destination "$package\docs" -Force
    Copy-Item -LiteralPath 'third_party\TrafficMonitor-LICENSE' -Destination "$package\third_party" -Force
    Compress-Archive -Path "$package\*" -DestinationPath 'dist\VgnVxeBattery-x64.zip' -Force
    Write-Output (Join-Path $repo 'dist\VgnVxeBattery-x64.zip')
} finally { Pop-Location }
