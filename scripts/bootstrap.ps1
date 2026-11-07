[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$baseline = (Get-Content -LiteralPath "$root/vcpkg.json" -Raw | ConvertFrom-Json).'builtin-baseline'
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = Join-Path $root '.tools/vcpkg'
}
if (-not (Test-Path -LiteralPath "$env:VCPKG_ROOT/.git")) {
    if (Test-Path -LiteralPath $env:VCPKG_ROOT) {
        throw "VCPKG_ROOT exists but is not a git checkout: $env:VCPKG_ROOT"
    }
    New-Item -ItemType Directory -Force (Split-Path $env:VCPKG_ROOT -Parent) | Out-Null
    git clone https://github.com/microsoft/vcpkg.git $env:VCPKG_ROOT
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone failed' }
    git -C $env:VCPKG_ROOT checkout --detach $baseline
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg baseline checkout failed' }
}
if (-not (Test-Path -LiteralPath "$env:VCPKG_ROOT/vcpkg.exe")) {
    & "$env:VCPKG_ROOT/bootstrap-vcpkg.bat" -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
}
