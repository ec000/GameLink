param([string]$PayloadDirectory, [string]$DataDirectory, [string]$OriginalBackup)
# Destructive fixtures only within the fresh installer CI machine.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') { throw 'Disposable Windows runner required' }
$shell = "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe"
$worlds = Get-Content (Join-Path $PayloadDirectory 'manifest.json') -Raw | ConvertFrom-Json
$moonName = "$($worlds.moon.id).moon"
$script = Join-Path $PayloadDirectory 'apply-zerotier-roots.ps1'
function Invoke-Helper([string]$Path, [int]$Expected) {
    $p = Start-Process $shell -ArgumentList @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', "`"$Path`"") -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne $Expected) { throw "Unexpected roots helper result: $($p.ExitCode), expected $Expected" }
}
$originalIdentity = (Get-FileHash (Join-Path $DataDirectory 'identity.secret') -Algorithm SHA256).Hash
$before = @(Get-ChildItem (Join-Path $DataDirectory 'gamelink-root-backups') -Directory).Count
Invoke-Helper $script 0
if (@(Get-ChildItem (Join-Path $DataDirectory 'gamelink-root-backups') -Directory).Count -ne $before) { throw 'Identical roots created unnecessary backup/restart' }
# Tampering is refused before any service/file mutation.
$tamper = Join-Path $env:RUNNER_TEMP ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $tamper | Out-Null
Copy-Item (Join-Path $PayloadDirectory '*') $tamper
[IO.File]::WriteAllText((Join-Path $tamper 'planet'), 'tampered')
Invoke-Helper (Join-Path $tamper 'apply-zerotier-roots.ps1') 31
Remove-Item $tamper -Recurse -Force
# Fail after the planet has been replaced: a file blocks the moons.d directory.
# The original planet from installation is used to prove rollback byte-for-byte.
Stop-Service ZeroTierOneService
(Get-Service ZeroTierOneService).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
$moons = Join-Path $DataDirectory 'moons.d'
$savedMoons = Join-Path $DataDirectory 'moons-ci-save'
Move-Item $moons $savedMoons
$planet = Join-Path $DataDirectory 'planet'
$priorPlanet = Join-Path $OriginalBackup 'planet'
if (-not (Test-Path $priorPlanet)) { throw 'Official planet backup missing' }
Copy-Item $priorPlanet $planet -Force
$priorHash = (Get-FileHash $planet -Algorithm SHA256).Hash
try {
    [IO.File]::WriteAllText($moons, 'blocking fixture')
    Invoke-Helper $script 35
    if ((Get-FileHash $planet -Algorithm SHA256).Hash -ne $priorHash) { throw 'Rollback failed to restore planet' }
    if ((Get-Service ZeroTierOneService).Status -ne 'Stopped') { throw 'Rollback started previously stopped service' }
} finally {
    Remove-Item $moons -Force
    Move-Item $savedMoons $moons
}
Invoke-Helper $script 0
if ((Get-Service ZeroTierOneService).Status -ne 'Stopped') { throw 'Apply started previously stopped service' }
Start-Service ZeroTierOneService
(Get-Service ZeroTierOneService).WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
if ((Get-FileHash (Join-Path $DataDirectory 'identity.secret') -Algorithm SHA256).Hash -ne $originalIdentity) { throw 'Roots helper changed private identity' }
Write-Host 'PASS: root hashes, idempotence, tamper rejection, partial-write rollback, stopped-service preservation and identity preservation.'
