param([Parameter(Mandatory=$true)][string]$QtPrefix, [string]$InnoCompiler = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$stage = Join-Path $root 'build/stage'
cmake -S . -B build/windows -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build build/windows --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build/windows -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Core/Qt client tests failed' }
cmake --install build/windows --config Release --prefix "$stage"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
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
Compress-Archive -Path build/stage/* -DestinationPath build/GameLink-0.1.0-Windows.zip -Force
if ($InnoCompiler) {
  & $InnoCompiler "/DStageDir=$root\build\stage" deploy/GameLink.iss
  if ($LASTEXITCODE -ne 0) { throw 'Installer generation failed' }
}
Write-Output 'Qt 6 package staged and startup checked. Real ZeroTier, Windows desktop interaction and game acceptance tests remain required.'
