[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Preset,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CTestArgs
)
$ErrorActionPreference = 'Stop'
try {
    $ctest = (Get-Command ctest -CommandType Application -ErrorAction Stop).Source
} catch {
    [Console]::Error.WriteLine('CTest executable not found on PATH.')
    exit 127
}
$resultsDir = Join-Path (Get-Location).Path 'out/test-results'
$runDir = Join-Path $resultsDir ([guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$log = Join-Path $runDir 'ctest.log'
$errorLog = Join-Path $runDir 'ctest.err'
# Native stderr redirection must not turn CTest failures into PowerShell exceptions.
$oldPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $ctest --preset $Preset @CTestArgs --output-on-failure --test-output-size-failed 0 --output-junit (Join-Path $runDir 'results.xml') 1> $log 2> $errorLog
    $status = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $oldPreference
}
& cmake "-DRUN_DIR=$runDir" "-DSTATUS=$status" -P "$PSScriptRoot/test-summary.cmake"
if ($LASTEXITCODE -ne 0) { exit 1 }
exit $status
