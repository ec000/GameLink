param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$package = Get-Content (Join-Path $root 'deploy/zerotier-package.json') -Raw | ConvertFrom-Json
if ($package.version -notmatch '^\d+\.\d+\.\d+$' -or $package.sha256 -notmatch '^[a-f0-9]{64}$' -or
    -not $package.url.StartsWith('https://download.zerotier.com/RELEASES/')) { throw 'Invalid pinned ZeroTier package manifest' }
$destinationPath = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
if (-not (Test-Path -LiteralPath $destinationPath)) {
    $temporary = "$destinationPath.$([Guid]::NewGuid().ToString('N')).download"
    try {
        Invoke-WebRequest -Uri $package.url -OutFile $temporary -MaximumRedirection 3
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) { throw 'Downloaded ZeroTier MSI SHA-256 mismatch' }
        Move-Item -LiteralPath $temporary -Destination $destinationPath
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
}
if ((Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.sha256) { throw 'Cached ZeroTier MSI SHA-256 mismatch; remove the invalid cache and retry' }
$signature = Get-AuthenticodeSignature -LiteralPath $destinationPath
if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate) { throw "ZeroTier MSI signature check failed: $($signature.Status)" }
$organization = [regex]::Match($signature.SignerCertificate.Subject, '(?:^|,\s*)O=(?:"([^"]+)"|([^,]+))')
$publisher = if ($organization.Groups[1].Success) { $organization.Groups[1].Value } else { $organization.Groups[2].Value }
if (($publisher -replace '[\s,.]', '').ToLowerInvariant() -ne 'zerotierinc') { throw 'ZeroTier MSI certificate organization mismatch' }
$installer = $null; $database = $null
function Read-PackageProperty([string]$Name) {
    $view = $null; $record = $null
    try {
        $query = 'SELECT `Value` FROM `Property` WHERE `Property` = ''{0}''' -f $Name
        $view = $database.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $database, @($query))
        $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
        $record = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)
        return $record.GetType().InvokeMember('StringData', 'GetProperty', $null, $record, @(1))
    } finally {
        if ($record) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) }
        if ($view) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
    }
}
try {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($destinationPath, 0))
    $name = Read-PackageProperty 'ProductName'
    $version = Read-PackageProperty 'ProductVersion'
    $manufacturer = Read-PackageProperty 'Manufacturer'
    if ($name -ne 'ZeroTier One' -or $version -ne $package.version -or
        ($manufacturer -replace '[\s,.]', '').ToLowerInvariant() -ne 'zerotierinc') { throw 'ZeroTier MSI product/version/manufacturer mismatch' }
} finally {
    if ($database) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
    if ($installer) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer) }
}
Write-Host "Verified official ZeroTier One $($package.version) MSI (SHA-256, Authenticode and product metadata)."
[pscustomobject]@{ Path=$destinationPath; Version=$package.version; Sha256=$package.sha256 }
