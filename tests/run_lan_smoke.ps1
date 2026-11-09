param(
    [Parameter(Mandatory)] [string] $Headless,
    [Parameter(Mandatory)] [string] $WorkDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not [IO.Path]::IsPathFullyQualified($Headless) -or -not (Test-Path -LiteralPath $Headless -PathType Leaf)) {
    throw "Headless must be an absolute path to an existing executable: $Headless"
}
if (-not (Test-Path -LiteralPath $WorkDirectory -PathType Container)) {
    throw "WorkDirectory must be an existing directory: $WorkDirectory"
}
$WorkDirectory = (Resolve-Path -LiteralPath $WorkDirectory).Path
$testDirectory = Join-Path $WorkDirectory ("lan-smoke-" + [guid]::NewGuid().ToString('N'))
$processes = [Collections.Generic.List[object]]::new()
$created = $false

function Start-SmokeProcess([string] $Label, [string[]] $Arguments, [bool] $HostProcess = $false) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Headless
    $start.WorkingDirectory = $WorkDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Failed to start $Label" }
    $entry = [pscustomobject]@{
        Label = $Label; Process = $process; FirstLine = $null; Output = $null
        Error = $process.StandardError.ReadToEndAsync(); Prefix = ''
    }
    $processes.Add($entry)
    if ($HostProcess) { $entry.FirstLine = $process.StandardOutput.ReadLineAsync() }
    else { $entry.Output = $process.StandardOutput.ReadToEndAsync() }
    return $entry
}

function Wait-HostReady($Entry, [int] $Port, [int] $DiscoveryPort) {
    if (-not $Entry.FirstLine.Wait(10000)) { throw "$($Entry.Label) readiness timed out" }
    $line = $Entry.FirstLine.Result
    $Entry.Prefix = if ($null -eq $line) { '' } else { "$line`n" }
    $Entry.Output = $Entry.Process.StandardOutput.ReadToEndAsync()
    if ($line -cne "hosting port=$Port discovery-port=$DiscoveryPort") {
        throw "$($Entry.Label) did not announce the expected endpoint"
    }
}

function Wait-SmokeProcess($Entry, [int] $TimeoutMs = 10000) {
    if (-not $Entry.Process.WaitForExit($TimeoutMs)) { throw "$($Entry.Label) timed out" }
    $Entry.Process.WaitForExit()
    if ($Entry.Process.ExitCode -ne 0) { throw "$($Entry.Label) exited with code $($Entry.Process.ExitCode)" }
    return $Entry.Prefix + $Entry.Output.GetAwaiter().GetResult()
}

function Get-GuestResult([string] $Output, [string] $Label) {
    $joined = [regex]::Match($Output, '(?m)^joined player=(\d+) token=(\d+) position=(-?\d+,-?\d+,-?\d+) tick=\d+\r?$')
    $final = [regex]::Match($Output, '(?m)^final player=(\d+) token=(\d+) position=(-?\d+,-?\d+,-?\d+) tick=\d+ resolved=1/1\r?$')
    if (-not $joined.Success -or -not $final.Success) { throw "$Label did not join and resolve its command" }
    $id = $joined.Groups[1].Value
    $token = $joined.Groups[2].Value
    $position = $joined.Groups[3].Value
    if ($id -eq '0' -or $token -eq '0' -or $final.Groups[1].Value -ne $id -or
        $final.Groups[2].Value -ne $token -or $final.Groups[3].Value -ne $position) {
        throw "$Label identity or position changed during wait"
    }
    if ($Output -notmatch "(?m)^receipt player=$id sequence=\d+ outcome=You wait\. tick=\d+\r?$") {
        throw "$Label did not receive a successful terminal wait outcome"
    }
    return [pscustomobject]@{ Id = $id; Token = $token; Position = $position }
}

try {
    [void] [IO.Directory]::CreateDirectory($testDirectory)
    $created = $true
    $save = Join-Path $testDirectory 'world.sqlite'
    $aliceProfile = Join-Path $testDirectory 'alice.profile'
    $bobProfile = Join-Path $testDirectory 'bob.profile'

    # Keep both ports reserved until immediately before launching the host.
    $gameSocket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    $discoverySocket = $null
    try {
        $discoverySocket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
        $port = $gameSocket.Client.LocalEndPoint.Port
        $discoveryPort = $discoverySocket.Client.LocalEndPoint.Port
    } finally {
        $gameSocket.Dispose()
        if ($null -ne $discoverySocket) { $discoverySocket.Dispose() }
    }

    $host1 = Start-SmokeProcess 'host' @('--host', '--port', "$port", '--discovery-port', "$discoveryPort", '--save', $save, '--name', 'Host', '--run-for', '5000') $true
    Wait-HostReady $host1 $port $discoveryPort
    $alice = Start-SmokeProcess 'Alice' @('--join', '127.0.0.1', '--port', "$port", '--name', 'Alice', '--profile', $aliceProfile, '--run-for', '2500', '--command', 'wait')
    $bob = Start-SmokeProcess 'Bob' @('--join', '127.0.0.1', '--port', "$port", '--name', 'Bob', '--profile', $bobProfile, '--run-for', '2500', '--command', 'wait')
    $discovery = Start-SmokeProcess 'discovery' @('--discover', '--discovery-address', '127.0.0.1', '--discovery-port', "$discoveryPort", '--run-for', '1200')
    $discoveryOutput = Wait-SmokeProcess $discovery
    if ($discoveryOutput -notmatch "(?m)^session 127\.0\.0\.1:$port name=Host players=\d+/\d+ compatible=1\r?$") {
        throw 'Directed discovery did not find the compatible host'
    }
    $aliceResult = Get-GuestResult (Wait-SmokeProcess $alice) 'Alice'
    $bobResult = Get-GuestResult (Wait-SmokeProcess $bob) 'Bob'
    if ($aliceResult.Id -eq $bobResult.Id -or $aliceResult.Token -eq $bobResult.Token) { throw 'Guests did not receive distinct identities' }
    [void] (Wait-SmokeProcess $host1)
    if (-not (Test-Path -LiteralPath $save -PathType Leaf) -or
        -not (Test-Path -LiteralPath $aliceProfile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $bobProfile -PathType Leaf)) { throw 'Host save or guest profile missing' }
    Write-Host 'LAN discovery, concurrent guests, waits and checkpoint passed.'

    $host2 = Start-SmokeProcess 'restarted host' @('--host', '--port', "$port", '--discovery-port', "$discoveryPort", '--load', $save, '--name', 'Host', '--run-for', '3000') $true
    Wait-HostReady $host2 $port $discoveryPort
    $resume = Start-SmokeProcess 'resumed Alice' @('--join', '127.0.0.1', '--port', "$port", '--name', 'Alice', '--profile', $aliceProfile, '--run-for', '1500', '--command', 'wait')
    $resumedResult = Get-GuestResult (Wait-SmokeProcess $resume) 'resumed Alice'
    if ($resumedResult.Id -ne $aliceResult.Id -or $resumedResult.Token -ne $aliceResult.Token -or
        $resumedResult.Position -ne $aliceResult.Position) { throw 'Alice did not resume her saved identity and position' }
    [void] (Wait-SmokeProcess $host2)
    Write-Host 'Host restart and profile resume passed.'
} catch {
    $failure = $_
    foreach ($entry in $processes) {
        if (-not $entry.Process.HasExited) { $entry.Process.Kill($true) }
    }
    foreach ($entry in $processes) {
        $entry.Process.WaitForExit()
        if ($null -eq $entry.Output) {
            if ($null -ne $entry.FirstLine) {
                $line = $entry.FirstLine.GetAwaiter().GetResult()
                if ($null -ne $line) { $entry.Prefix = "$line`n" }
            }
            $entry.Output = $entry.Process.StandardOutput.ReadToEndAsync()
        }
        $stdout = $entry.Prefix + $entry.Output.GetAwaiter().GetResult()
        $stderr = $entry.Error.GetAwaiter().GetResult()
        [Console]::Error.WriteLine("--- $($entry.Label) (exit $($entry.Process.ExitCode)) stdout ---`n$stdout`n--- stderr ---`n$stderr")
    }
    throw $failure
} finally {
    foreach ($entry in $processes) { $entry.Process.Dispose() }
    if ($created) { Remove-Item -LiteralPath $testDirectory -Recurse -Force }
}
