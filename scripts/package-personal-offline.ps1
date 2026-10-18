param([Parameter(Mandatory=$true)][string]$Archive, [Parameter(Mandatory=$true)][string]$InnoCompiler, [switch]$PersonalUseOnly)
$ErrorActionPreference = 'Stop'
if (-not $PersonalUseOnly) { throw 'This package is only for the confirmed personal non-commercial private use; no public redistribution rights are asserted' }
$root = Split-Path -Parent $PSScriptRoot
$base = Get-Content (Join-Path $root 'deploy/personal-offline-base.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $base.sha256) { throw 'Previously verified GameLink base ZIP checksum mismatch' }
$stage = Join-Path $root 'build/personal-stage'
if (Test-Path $stage) { throw 'Personal staging directory already exists; use a fresh build workspace' }
Expand-Archive -LiteralPath $Archive -DestinationPath $stage
foreach ($required in @('GameLink.exe', 'Qt6Core.dll', 'Qt6Widgets.dll', 'plugins/platforms/qwindows.dll')) {
    if (-not (Test-Path (Join-Path $stage $required))) { throw "Missing base dependency: $required" }
}
$zeroTier = & (Join-Path $PSScriptRoot 'get-zerotier-package.ps1') -Destination (Join-Path $root 'build/cache/ZeroTier-One.msi')
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'prerequisites') | Out-Null
Copy-Item -LiteralPath $zeroTier.Path -Destination (Join-Path $stage 'prerequisites/ZeroTier-One.msi')
Copy-Item -Path (Join-Path $root 'third_party/*') -Destination (Join-Path $stage 'licenses') -Recurse -Force
$licenseDir = Join-Path $stage 'licenses/zerotier'
$notice = @'
GameLink personal non-commercial offline package

This private package contains the original, unmodified official ZeroTier One
1.16.2 MSI, including its service, driver and desktop UI. It is prepared only
for the owner's confirmed personal non-commercial use, not public distribution.
No commercial, OEM or public redistribution permission is asserted or granted.
ZeroTier's original Source-Available and component license terms still apply.
GameLink is not endorsed by or affiliated with ZeroTier, Inc.

The installer extracts the embedded MSI and checks its pinned SHA-256 before
requesting administrator permission. No MSI download is needed at installation.
Existing same/newer standard versions are retained. GameLink uninstall preserves
the independently installed ZeroTier. No planet/moon files are replaced.

Official MSI: https://download.zerotier.com/RELEASES/1.16.2/dist/ZeroTier%20One.msi
SHA-256: 42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670
Source: https://github.com/zerotier/ZeroTierOne/tree/1.16.2
Desktop UI source: https://github.com/zerotier/DesktopUI
See LICENSE-SOURCE-AVAILABLE.md and LICENSE-MPL-2.0.txt for the original terms.
'@
Set-Content -LiteralPath (Join-Path $licenseDir 'NOTICE.txt') -Value $notice -Encoding utf8
$license = $notice + "`n`n" + (Get-Content (Join-Path $licenseDir 'LICENSE-SOURCE-AVAILABLE.md') -Raw) + "`n`n" + (Get-Content (Join-Path $licenseDir 'LICENSE-MPL-2.0.txt') -Raw)
$licenseFile = Join-Path $licenseDir 'INSTALLER-LICENSE.txt'
Set-Content -LiteralPath $licenseFile -Value $license -Encoding utf8
$manifest = Get-Content (Join-Path $root 'deploy/zerotier-package.json') -Raw | ConvertFrom-Json
$manifest.distribution = 'personal-noncommercial-private-offline'
$manifest | ConvertTo-Json | Set-Content (Join-Path $stage 'prerequisites/zerotier-package.json') -Encoding utf8
& $InnoCompiler '/DBundleZeroTier' '/DPersonalUseOnly' "/DStageDir=$stage" "/DMyAppVersion=$($base.version)" "/DZeroTierMsi=$($zeroTier.Path)" "/DZeroTierVersion=$($zeroTier.Version)" "/DZeroTierSHA256=$($zeroTier.Sha256)" "/DInstallerLicenseFile=$licenseFile" (Join-Path $root 'deploy/GameLink.iss')
if ($LASTEXITCODE -ne 0) { throw 'Personal offline installer compilation failed' }
Write-Host 'Built private personal offline EXE using the unchanged verified Qt application and original official MSI. Do not publicly upload the raw EXE or staging directory.'
