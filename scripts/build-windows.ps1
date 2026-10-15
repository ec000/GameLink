param([Parameter(Mandatory=$true)][string]$QtPrefix, [string]$InnoCompiler = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
cmake -S . -B build/windows -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build build/windows --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build/windows -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
cmake --install build/windows --config Release --prefix build/stage
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
Compress-Archive -Path build/stage/* -DestinationPath build/GameLink-0.1.0-Windows.zip -Force
if ($InnoCompiler) {
  & $InnoCompiler "/DStageDir=$root\build\stage" deploy/GameLink.iss
  if ($LASTEXITCODE -ne 0) { throw 'Installer generation failed' }
}
Write-Output 'Package staged. Windows GUI, real ZeroTier and game acceptance tests remain required.'
