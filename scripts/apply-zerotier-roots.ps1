# Template materialized with pinned hashes by package-personal-offline.ps1.
# Windows PowerShell 5.1, elevated by the installer's explicit customroots task.
param([switch]$ValidatePayloadOnly)
$ErrorActionPreference = 'Stop'
$planetHash = '@@PLANET_SHA256@@'
$moonHash = '@@MOON_SHA256@@'
$moonName = '@@MOON_NAME@@'
$serviceName = 'ZeroTierOneService'
$dataDir = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'ZeroTier/One'
$backup = $null
$changed = @()
$wasRunning = $false
$serviceTouched = $false
$phase = 1
function Assert-PlainPath([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse path refused' }
        }
        $parent = Split-Path -Parent $current
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Check-Hash([string]$Path, [string]$Hash) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash) { throw 'Root payload checksum mismatch' }
}
function Stop-ZeroTier {
    $s = Get-Service $serviceName
    if ($s.Status -ne 'Stopped') {
        $stopFailure = $null
        try { Stop-Service $serviceName -ErrorAction Stop } catch { $stopFailure = $_ }
        # ZeroTier can close the SCM control pipe while exiting (Win32 109).
        # Accept this only when SCM actually confirms Stopped within the bound.
        try { (Get-Service $serviceName).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30)) }
        catch { if ($stopFailure) { throw $stopFailure }; throw }
    }
}
function Start-ZeroTier {
    if ((Get-Service $serviceName).Status -ne 'Running') { Start-Service $serviceName }
    (Get-Service $serviceName).WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
}
try {
    # ShellExecute/UAC may inherit a PSModulePath belonging to PowerShell 7.
    # Load only the Windows host's protected, version-matched system modules.
    foreach ($module in @('Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Security')) {
        Import-Module ([IO.Path]::Combine($PSHOME, "Modules\$module\$module.psd1")) -Force -ErrorAction Stop
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { exit 20 }
    if ($planetHash -notmatch '^[a-f0-9]{64}$' -or $moonHash -notmatch '^[a-f0-9]{64}$' -or $moonName -notmatch '^[a-f0-9]{16}\.moon$') { exit 21 }
    # Installer temp directories can legitimately be redirected. Source bytes
    # are pinned and locked by Inno; enforce plain paths on machine destinations.
    Check-Hash (Join-Path $PSScriptRoot 'planet') $planetHash
    Check-Hash (Join-Path $PSScriptRoot $moonName) $moonHash
    if ($ValidatePayloadOnly) { exit 0 }
    $phase = 2
    Assert-PlainPath $dataDir
    if (-not (Test-Path -LiteralPath $dataDir -PathType Container)) { exit 22 }
    # Only the official standard data location. Never infer a writable location
    # from untrusted service arguments, user settings or a supplied path.
    $service = Get-Service $serviceName
    $standardExe = Join-Path $dataDir 'zerotier-one_x64.exe'
    $serviceCommand = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\ZeroTierOneService' -Name ImagePath).ImagePath
    if ($serviceCommand -notmatch '^(?:"([^"]+)"|(\S+))(?:\s+-s)?\s*$') { exit 22 }
    $serviceExe = if ($Matches[1]) { $Matches[1] } else { $Matches[2] }
    if ($serviceExe -ine $standardExe) { exit 22 }
    if ($service.Status -notin @('Running', 'Stopped')) { exit 22 }
    $wasRunning = $service.Status -eq 'Running'
    $items = @(
        @{Name='planet'; Path=(Join-Path $dataDir 'planet'); Hash=$planetHash},
        @{Name=$moonName; Path=(Join-Path $dataDir "moons.d/$moonName"); Hash=$moonHash}
    )
    foreach ($item in $items) {
        Assert-PlainPath $item.Path
        if (-not (Test-Path -LiteralPath $item.Path) -or (Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.Hash) { $changed += $item }
    }
    if ($changed.Count -eq 0) { exit 0 } # Do not restart on identical reinstall.
    $phase = 3
    $backupRoot = Join-Path $dataDir 'gamelink-root-backups'
    Assert-PlainPath $backupRoot
    $backup = Join-Path $backupRoot ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    # Explicit administrator/SYSTEM-only backup, independent of machine locale.
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $principal = New-Object Security.Principal.SecurityIdentifier($sid)
        $rule = New-Object Security.AccessControl.FileSystemAccessRule($principal, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $backup -AclObject $acl
    $phase = 4
    $serviceTouched = $true
    Stop-ZeroTier
    # Back up *all* destinations before making the first change.
    foreach ($item in $changed) {
        $item.Existed = Test-Path -LiteralPath $item.Path
        if ($item.Existed) { Copy-Item -LiteralPath $item.Path -Destination (Join-Path $backup $item.Name) }
    }
    $changed | Select-Object Name, Path, Existed | ConvertTo-Json | Set-Content (Join-Path $backup 'manifest.json') -Encoding UTF8
    $phase = 5
    foreach ($item in $changed) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $item.Path) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $item.Name) -Destination $item.Path -Force
        Check-Hash $item.Path $item.Hash
    }
    $phase = 6
    if ($wasRunning) { Start-ZeroTier }
    exit 0
} catch {
    $failure = "ZT_ROOTS_FAILURE stage=$phase type=$($_.Exception.GetType().Name): $($_.Exception.Message)"
    Write-Host $failure
    try {
        Assert-PlainPath $dataDir
        if (Test-Path -LiteralPath $dataDir -PathType Container) {
            # Unique CreateNew file inside the service directory: never follow
            # or overwrite a pre-created diagnostic file during elevation.
            $errorFile = Join-Path $dataDir ('gamelink-roots-error-' + [Guid]::NewGuid().ToString('N') + '.log')
            $stream = [IO.File]::Open($errorFile, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            try { $bytes = [Text.Encoding]::UTF8.GetBytes($failure); $stream.Write($bytes, 0, $bytes.Length) }
            finally { $stream.Dispose() }
        }
    } catch { } # Diagnostic failure must not interrupt rollback.
    # No identity, token, networks.d, other moons or controller files are copied.
    try {
        if ($serviceTouched) {
            Stop-ZeroTier
            foreach ($item in $changed) {
                if (-not $item.ContainsKey('Existed')) { continue }
                if ($item.Existed) {
                    $saved = Join-Path $backup $item.Name
                    if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination $item.Path -Force }
                } elseif (Test-Path -LiteralPath $item.Path) { Remove-Item -LiteralPath $item.Path -Force }
            }
            if ($wasRunning) { Start-ZeroTier }
        }
    } catch { exit 24 } # Rollback/service restoration requires manual attention.
    exit (30 + $phase)
}
