# Destructive lifecycle test: only on a fresh, disposable GitHub Actions Windows VM.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') { throw 'This test only runs on disposable Windows GitHub Actions runners' }
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'build/installer-tests'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$setup = Get-ChildItem (Join-Path $root 'build/installer/GameLink-*-Setup.exe') | Select-Object -First 1
if (-not $setup) { throw 'Missing compiled EXE installer' }
$target = Join-Path $env:LOCALAPPDATA 'Programs/GameLink-Installer-CI'
$msi = Join-Path $root 'build/cache/ZeroTier-One.msi'
$serviceName = 'ZeroTierOneService'
$uninstallRoots = @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')
foreach ($registryRoot in $uninstallRoots) {
    if (Test-Path $registryRoot) {
        $existing = Get-ChildItem $registryRoot | Get-ItemProperty | Where-Object { $_.DisplayName -like '*ZeroTier*' }
        if ($existing) { throw 'Refusing lifecycle test: ZeroTier already installed' }
    }
}
if ((Get-Service $serviceName -ErrorAction SilentlyContinue) -or (Test-Path "$env:ProgramData/ZeroTier/One/identity.secret") -or (Test-Path $target)) {
    throw 'Refusing lifecycle test: pre-existing service, identity or test installation'
}
function Run-Checked([string]$File, [string[]]$Arguments, [switch]$Elevated) {
    $start = @{FilePath=$File; ArgumentList=$Arguments; PassThru=$true}
    if ($Elevated) { $start.Verb = 'RunAs' }
    $process = Start-Process @start
    if (-not $process.WaitForExit(180000)) { $process.Kill(); throw "Timed out: $File" }
    if ($process.ExitCode -notin @(0, 3010)) { throw "Installer failed: $($process.ExitCode), $File" }
}
function Run-Setup([string]$LogName, [string[]]$Extra) {
    Run-Checked $setup.FullName (@('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$target`"", "/LOG=`"$logs/$LogName.log`"") + $Extra)
}
function Remove-GameLink([string]$LogName) {
    Run-Checked (Join-Path $target 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$logs/$LogName.log`"")
    if (Test-Path (Join-Path $target 'GameLink.exe')) { throw 'GameLink EXE remained after uninstall' }
}
$installedZeroTier = $false
try {
    Run-Setup '01-opt-out' @('/MERGETASKS=!zerotier')
    if (Get-Service $serviceName -ErrorAction SilentlyContinue) { throw 'Opt-out unexpectedly installed ZeroTier' }
    if (-not (Select-String -LiteralPath "$logs/01-opt-out.log" -Pattern 'ZT_PREREQ_OPT_OUT' -SimpleMatch -Quiet)) { throw 'Missing opt-out evidence' }
    Remove-GameLink '02-remove-opt-out'
    # No task override: proves the default selection installs the bundled prerequisite.
    Run-Setup '03-default-install' @()
    $installedZeroTier = $true
    if (-not (Select-String -LiteralPath "$logs/03-default-install.log" -Pattern 'ZT_PREREQ_SUCCESS' -SimpleMatch -Quiet)) { throw 'Missing real MSI installation evidence' }
    $service = Get-Service $serviceName
    $service.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
    $dataDir = Join-Path $env:ProgramData 'ZeroTier/One'
    # Confirm the actual official Windows binary's license, rather than inferring
    # it from repository defaults (Windows project flags can differ from make).
    $licenseProcess = Start-Process -FilePath (Join-Path $dataDir 'zerotier-one_x64.exe') -ArgumentList @('-q', '-h') -PassThru -Wait -NoNewWindow -RedirectStandardOutput "$logs/zerotier-binary-license.log" -RedirectStandardError "$logs/zerotier-binary-license-error.log"
    $licenseText = (Get-Content "$logs/zerotier-binary-license.log" -Raw) + (Get-Content "$logs/zerotier-binary-license-error.log" -Raw)
    if ($licenseText -notmatch 'Mozilla Public License' -or $licenseText -match 'Source-Available License') { throw 'Official Windows binary is not an MPL-only agent; redistribution terms require separate review before packaging' }
    $identityPath = Join-Path $dataDir 'identity.secret'
    for ($attempt = 0; $attempt -lt 30 -and -not (Test-Path $identityPath); $attempt++) { Start-Sleep -Seconds 1 }
    $identityHash = (Get-FileHash $identityPath -Algorithm SHA256).Hash
    # Verify the local service API without logging its credential or identity contents.
    $token = [IO.File]::ReadAllText((Join-Path $dataDir 'authtoken.secret')).Trim()
    $status = $null
    for ($attempt = 0; $attempt -lt 30 -and -not $status; $attempt++) {
        try { $status = Invoke-RestMethod 'http://127.0.0.1:9993/status' -Headers @{'X-ZT1-Auth'=$token} -NoProxy -TimeoutSec 2 -MaximumRedirection 0 }
        catch { Start-Sleep -Seconds 1 }
    }
    $manifest = Get-Content (Join-Path $root 'deploy/zerotier-package.json') -Raw | ConvertFrom-Json
    if ($status.version -ne $manifest.version -or $status.address -notmatch '^[a-f0-9]{10}$') { throw 'Unexpected local API version/address' }
    $token = $null
    Run-Checked (Join-Path $target 'GameLink.exe') @('--self-test')
    # Service control, like MSI installation, needs an elevated system tool.
    Run-Checked "$env:SystemRoot/System32/sc.exe" @('stop', $serviceName) -Elevated
    (Get-Service $serviceName).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(30))
    Run-Setup '04-repeat-install' @('/TASKS=zerotier')
    if (-not (Select-String -LiteralPath "$logs/04-repeat-install.log" -Pattern 'ZT_PREREQ_SKIP' -SimpleMatch -Quiet)) { throw 'Repeat install did not skip existing same version' }
    if ((Get-FileHash $identityPath -Algorithm SHA256).Hash -ne $identityHash) { throw 'Repeat install changed ZeroTier identity' }
    if ((Get-Service $serviceName).Status -ne 'Stopped') { throw 'Repeat install unexpectedly started an existing stopped service' }
    Run-Checked "$env:SystemRoot/System32/sc.exe" @('start', $serviceName) -Elevated
    (Get-Service $serviceName).WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
    Remove-GameLink '05-remove-gamelink'
    if ((Get-Service $serviceName).Status -ne 'Running' -or (Get-FileHash $identityPath -Algorithm SHA256).Hash -ne $identityHash) { throw 'GameLink uninstall affected ZeroTier' }
    Write-Host 'PASS: offline EXE opt-out, default real MSI installation, running service/API, bundled GUI startup, idempotent reinstall and independent uninstall.'
} finally {
    if (Test-Path (Join-Path $target 'unins000.exe')) { Remove-GameLink 'cleanup-gamelink' }
    if ($installedZeroTier -or (Get-Service $serviceName -ErrorAction SilentlyContinue)) {
        Run-Checked "$env:SystemRoot/System32/msiexec.exe" @('/x', "`"$msi`"", '/qn', '/norestart', 'REBOOT=ReallySuppress')
        if (Get-Service $serviceName -ErrorAction SilentlyContinue) { throw 'ZeroTier service remained after explicit MSI cleanup' }
        Write-Host 'PASS: explicit official MSI uninstall removed ZeroTier service on disposable runner.'
    }
}
