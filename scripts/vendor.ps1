# Maintainer command: downloads the exact upstream headers and licenses.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$libraries = @(
    @{
        Directory = 'unordered_dense'
        Repository = 'martinus/unordered_dense'
        Commit = '7c58a8b7a48e790fe72f6508eb3b46317cbba9d5'
        Files = @('include/ankerl/unordered_dense.h', 'include/ankerl/stl.h', 'LICENSE')
    },
    @{
        Directory = 'slotmap'
        Repository = 'SergeyMakeev/SlotMap'
        Commit = '122fb6960bc417f82f65a0469d38bfa05b506a25'
        Files = @('slot_map/slot_map.h', 'LICENSE')
    }
)
foreach ($library in $libraries) {
    foreach ($file in $library.Files) {
        $destination = Join-Path $root "third_party/$($library.Directory)/$file"
        New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
        Invoke-WebRequest "https://raw.githubusercontent.com/$($library.Repository)/$($library.Commit)/$file" -OutFile $destination
    }
}
