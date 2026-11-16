param(
    [Parameter(Mandatory)][string]$Headless,
    [Parameter(Mandatory)][string]$Sdl,
    [Parameter(Mandatory)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$root = Join-Path $WorkDirectory ('sdl-lan-' + [Guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($root) | Out-Null
$children = [System.Collections.Generic.List[object]]::new()

function FreePort {
    $socket = [System.Net.Sockets.UdpClient]::new(0)
    try { return $socket.Client.LocalEndPoint.Port } finally { $socket.Dispose() }
}
function Launch([string]$Label, [string]$Executable, [string[]]$Arguments) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($Executable)
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::Start($info)
    $child = [pscustomobject]@{ Label = $Label; Process = $process;
        Output = $process.StandardOutput.ReadToEndAsync(); Error = $process.StandardError.ReadToEndAsync() }
    $children.Add($child)
    return $child
}
function Finish($Child, [int]$Seconds = 25) {
    if (-not $Child.Process.WaitForExit($Seconds * 1000)) { throw "$($Child.Label) timed out" }
    $output = $Child.Output.GetAwaiter().GetResult()
    $errorText = $Child.Error.GetAwaiter().GetResult()
    [System.IO.File]::WriteAllText((Join-Path $root ($Child.Label + '.stdout')), $output)
    [System.IO.File]::WriteAllText((Join-Path $root ($Child.Label + '.stderr')), $errorText)
    if ($Child.Process.ExitCode -ne 0) {
        throw "$($Child.Label) exited $($Child.Process.ExitCode)`n$output`n$errorText"
    }
    return $output
}
function RequireMatch([string]$Text, [string]$Pattern) {
    if ($Text -notmatch $Pattern) { throw "Missing '$Pattern' in:`n$Text" }
}
try {
    $port = FreePort
    $discoveryPort = FreePort
    $serverProcess = Launch 'headless-host' $Headless @('--scenario', 'demo', '--host', '--port', "$port",
        '--discovery-port', "$discoveryPort", '--run-for', '16000', '--save', (Join-Path $root 'headless.sqlite'),
        '--user-dir', (Join-Path $root 'headless-user'))
    $browser = Launch 'sdl-discover' $Sdl @('--scenario', 'demo', '--smoke', '--discover',
        '--discovery-port', "$discoveryPort", '--discovery-address', '127.0.0.1',
        '--user-dir', (Join-Path $root 'browser-user'))
    RequireMatch (Finish $browser) 'SDL discovery smoke:.*ok'
    $profile = Join-Path $root 'sdl-profile.txt'
    $previousId = ''
    foreach ($attempt in 1..2) {
        $guest = Launch "sdl-guest-$attempt" $Sdl @('--scenario', 'demo', '--smoke', '--join', "127.0.0.1:$port",
            '--profile', $profile, '--name', 'SDL guest', '--user-dir', (Join-Path $root 'guest-user'))
        $output = Finish $guest
        RequireMatch $output 'SDL multiplayer smoke:.*ok'
        RequireMatch $output 'player=(\d+)'
        $id = [regex]::Match($output, 'player=(\d+)').Groups[1].Value
        if ($previousId -and $previousId -ne $id) { throw 'SDL reconnect changed player identity' }
        $previousId = $id
    }
    RequireMatch (Finish $serverProcess) 'hosting port='

    $port = FreePort
    $discoveryPort = FreePort
    $save = Join-Path $root 'sdl.sqlite'
    $profile = Join-Path $root 'headless-profile.txt'
    $previousId = ''
    foreach ($operation in @('--save', '--load')) {
        $label = if ($operation -eq '--save') { 'new' } else { 'loaded' }
        $serverProcess = Launch "sdl-host-$label" $Sdl @('--scenario', 'demo', '--smoke', '--host', '--port', "$port",
            '--discovery-port', "$discoveryPort", $operation, $save, '--user-dir', (Join-Path $root 'host-user'))
        # ENet retries while SDL initializes its renderer. The SDL host smoke waits
        # for an active peer and remains alive to deliver that peer's receipts.
        $guest = Launch "headless-guest-$label" $Headless @('--scenario', 'demo', '--join', "127.0.0.1:$port",
            '--profile', $profile, '--name', 'Headless guest', '--run-for', '3500', '--command', 'wait',
            '--user-dir', (Join-Path $root 'headless-guest-user'))
        $output = Finish $guest
        RequireMatch $output 'resolved=1/1'
        RequireMatch $output 'joined player=(\d+)'
        $id = [regex]::Match($output, 'joined player=(\d+)').Groups[1].Value
        if ($previousId -and $previousId -ne $id) { throw 'SDL save/load lost guest identity' }
        $previousId = $id
        RequireMatch (Finish $serverProcess) 'SDL multiplayer smoke:.*ok'
    }
    "SDL/headless LAN roundtrip: discovery, remote screens, reconnect, persistent host reload: ok"
    "Artifacts: $root"
} finally {
    foreach ($child in $children) {
        if (-not $child.Process.HasExited) { $child.Process.Kill($true); $child.Process.WaitForExit() }
        $child.Process.Dispose()
    }
}
