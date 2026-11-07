[CmdletBinding()]
param(
    [ValidateSet('win-msvc-debug', 'win-msvc-release', 'win-msvc-asan')]
    [string]$Preset = 'win-msvc-debug',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if (-not $env:VSCMD_VER) {
        $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools first.' }
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vs) { throw 'No Visual Studio C++ toolchain found.' }
        & "$vs/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    }
    if ($Preset -eq 'win-msvc-asan') {
        $compiler = (Get-Command cl.exe -ErrorAction Stop).Source
        $toolset = Split-Path (Split-Path (Split-Path (Split-Path $compiler -Parent) -Parent) -Parent) -Parent
        $runtime = Join-Path $toolset 'lib/x64/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib'
        if (-not (Test-Path -LiteralPath $runtime)) {
            throw 'Install C++ AddressSanitizer (Microsoft.VisualStudio.Component.VC.ASAN) in Visual Studio Installer, then rerun this preset.'
        }
    }
    . "$PSScriptRoot/bootstrap.ps1"
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    cmake -E copy_if_different "out/build/$Preset/compile_commands.json" compile_commands.json
    if ($LASTEXITCODE -ne 0) { throw 'Compilation database copy failed' }
    if (-not $SkipTests) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    }
} finally {
    Pop-Location
}
