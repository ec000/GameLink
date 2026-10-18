param([Parameter(Mandatory=$true)][string]$QtPrefix, [string]$InnoCompiler = '', [switch]$BundleZeroTier, [switch]$RedistributionAuthorized)
$ErrorActionPreference = 'Stop'
if ($BundleZeroTier -and -not $RedistributionAuthorized) { throw 'Offline bundling of the official Windows binary requires confirmed ZeroTier redistribution rights; default setup downloads directly from ZeroTier instead' }
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$stage = Join-Path $root 'build/stage'
$zeroTier = & (Join-Path $PSScriptRoot 'get-zerotier-package.ps1') -Destination (Join-Path $root 'build/cache/ZeroTier-One.msi')
$env:GAMELINK_TEST_ZT_MSI = $zeroTier.Path
cmake -S . -B build/windows -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
$versionMatch = Select-String -Path 'build/windows/CMakeCache.txt' -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$' | Select-Object -First 1
if (-not $versionMatch) { throw 'Cannot read configured GameLink version' }
$version = $versionMatch.Matches[0].Groups[1].Value
$archive = Join-Path $root "build/GameLink-$version-Windows.zip"
cmake --build build/windows --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build/windows -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
  if (Test-Path 'build/windows/qt-client-results.txt') { Get-Content 'build/windows/qt-client-results.txt' }
  throw 'Core/Qt client tests failed'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Cannot locate MSVC redistributable tools (vswhere)' }
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'Cannot locate Visual Studio C++ installation' }
$previousVC = $env:VCINSTALLDIR
try {
  $env:VCINSTALLDIR = (Join-Path $vsInstall 'VC') + '\'
  cmake --install build/windows --config Release --prefix "$stage"
  if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
} finally { $env:VCINSTALLDIR = $previousVC }
# App-local CRT avoids relying on the runner's globally installed VC runtime.
$crt = Get-ChildItem (Join-Path $vsInstall 'VC/Redist/MSVC') -Recurse -Directory -Filter 'Microsoft.VC143.CRT' |
  Where-Object { $_.FullName -match '[\\/]x64[\\/]' } | Select-Object -First 1
if (-not $crt) { throw 'Cannot locate x64 MSVC CRT DLLs' }
Copy-Item (Join-Path $crt.FullName '*.dll') $stage -Force
foreach ($required in @('GameLink.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
  'qt.conf', 'plugins/platforms/qwindows.dll', 'msvcp140.dll', 'vcruntime140.dll')) {
  if (-not (Test-Path (Join-Path $stage $required))) { throw "Missing staged dependency: $required" }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'prerequisites') | Out-Null
if ($BundleZeroTier) {
  Copy-Item -LiteralPath $zeroTier.Path -Destination (Join-Path $stage 'prerequisites/ZeroTier-One.msi') -Force
} elseif (Test-Path (Join-Path $stage 'prerequisites/ZeroTier-One.msi')) {
  Remove-Item -LiteralPath (Join-Path $stage 'prerequisites/ZeroTier-One.msi') -Force
}
Copy-Item -LiteralPath (Join-Path $root 'deploy/zerotier-package.json') -Destination (Join-Path $stage 'prerequisites/zerotier-package.json') -Force
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'licenses') | Out-Null
Copy-Item -Path (Join-Path $root 'third_party/*') -Destination (Join-Path $stage 'licenses') -Recurse -Force
# Run the deployed application without the SDK's bin directory in PATH.
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM
$pluginVariables = @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')
$previousPlugins = @{}
foreach ($name in $pluginVariables) { $previousPlugins[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  $env:QT_QPA_PLATFORM = 'windows'
  foreach ($name in $pluginVariables) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
  $process = Start-Process -FilePath "$root\build\stage\GameLink.exe" -ArgumentList '--self-test' -PassThru
  if (-not $process.WaitForExit(15000)) { $process.Kill(); throw 'Staged GUI self-test timed out' }
  if ($process.ExitCode -ne 0) { throw "Staged GUI self-test failed: $($process.ExitCode)" }
} finally {
  $env:PATH = $previousPath
  $env:QT_QPA_PLATFORM = $previousPlatform
  foreach ($name in $pluginVariables) { [Environment]::SetEnvironmentVariable($name, $previousPlugins[$name], 'Process') }
}
Compress-Archive -Path build/stage/* -DestinationPath $archive -Force
if ($InnoCompiler) {
  $bundleDefine = if ($BundleZeroTier) { "/DBundleZeroTier" } else { "/DDownloadZeroTier" }
  & $InnoCompiler $bundleDefine "/DStageDir=$root\build\stage" "/DMyAppVersion=$version" "/DZeroTierMsi=$($zeroTier.Path)" "/DZeroTierVersion=$($zeroTier.Version)" "/DZeroTierSHA256=$($zeroTier.Sha256)" "/DZeroTierURL=$((Get-Content deploy/zerotier-package.json -Raw | ConvertFrom-Json).url)" deploy/GameLink.iss
  if ($LASTEXITCODE -ne 0) { throw 'Installer generation failed' }
}
Write-Output 'Qt 6 package staged and startup checked. Real ZeroTier, Windows desktop interaction and game acceptance tests remain required.'
