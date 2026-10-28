param([ValidateSet('alice','bob')][string]$Account='alice', [switch]$CopyCredentialOnly, [switch]$PrepareOnly, [string]$DataRoot)
$ErrorActionPreference='Stop'
if (-not $env:LOCALAPPDATA) { throw 'Windows LOCALAPPDATA is required.' }
$binary=Join-Path $PSScriptRoot 'gamelink-server-local-demo.exe'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Extract the complete test ZIP before starting.' }
$base=if ($DataRoot) { [IO.Path]::GetFullPath($DataRoot) } else { Join-Path $env:LOCALAPPDATA 'GameLink' }
$directory=Join-Path $base 'LocalRoomTest'
foreach ($path in @($base,$directory)) {
    if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Reparse points are not supported for local test data.' }
}
$configFile=Join-Path $directory 'server.local-mock.json'
$credentialFile=Join-Path $directory 'credentials.local-mock.json'
$marker=Join-Path $directory 'local-mock.marker'
if ((Test-Path -LiteralPath $directory) -and (Get-ChildItem -LiteralPath $directory -Force)) {
    if (-not (Test-Path -LiteralPath $marker) -or [IO.File]::ReadAllText($marker) -ne 'GameLink local mock v1') { throw 'Existing directory is not this local test configuration. Refusing to overwrite it.' }
}
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$user=[Security.Principal.WindowsIdentity]::GetCurrent().User
$system=New-Object Security.Principal.SecurityIdentifier('S-1-5-18')
$acl=New-Object Security.AccessControl.DirectorySecurity
$acl.SetOwner($user);$acl.SetAccessRuleProtection($true,$false)
foreach ($identity in @($user,$system)) {
    $rule=New-Object Security.AccessControl.FileSystemAccessRule($identity,'FullControl','ContainerInherit, ObjectInherit','None','Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $directory -AclObject $acl
$utf8=New-Object Text.UTF8Encoding($false)
if (-not (Test-Path -LiteralPath $configFile) -and -not (Test-Path -LiteralPath $credentialFile)) {
    $random=[Security.Cryptography.RandomNumberGenerator]::Create()
    $sha=[Security.Cryptography.SHA256]::Create()
    try {
        $credentials=@{};$users=@();$nodes=@{alice='aaaaaaaaaa';bob='bbbbbbbbbb'}
        foreach ($name in @('alice','bob')) {
            $bytes=New-Object byte[] 32;$random.GetBytes($bytes)
            $token=[BitConverter]::ToString($bytes).Replace('-','').ToLowerInvariant()
            $credentials[$name]=$token
            $digest=[BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($token))).Replace('-','').ToLowerInvariant()
            $users+=@{id=$name;nodeId=$nodes[$name];tokenHash=$digest}
        }
        $config=@{listen='127.0.0.1:8080';controllerUrl='http://127.0.0.1:9993';controllerTokenFile='';stateFile=(Join-Path $directory 'state.local-mock.json');networks=@('abcdef1234000001','abcdef1234000002');users=$users;ttlSeconds=3600}
        [IO.File]::WriteAllText($configFile,($config | ConvertTo-Json -Depth 8),$utf8)
        [IO.File]::WriteAllText($credentialFile,($credentials | ConvertTo-Json),$utf8)
        [IO.File]::WriteAllText($marker,'GameLink local mock v1',$utf8)
    } finally { $random.Dispose();$sha.Dispose() }
}
if (-not (Test-Path -LiteralPath $configFile) -or -not (Test-Path -LiteralPath $credentialFile)) { throw 'Incomplete local test configuration; no files were overwritten.' }
$config=Get-Content -LiteralPath $configFile -Raw | ConvertFrom-Json
$credentials=Get-Content -LiteralPath $credentialFile -Raw | ConvertFrom-Json
if ($config.listen -ne '127.0.0.1:8080' -or $config.stateFile -ne (Join-Path $directory 'state.local-mock.json') -or $config.controllerTokenFile -ne '') { throw 'Unexpected test configuration. Refusing to start.' }
Write-Host 'MOCK ONLY: room creation/join/leave, no real ZeroTier authorization or game networking.'
Write-Host 'GameLink room service address: http://127.0.0.1:8080'
if ($PrepareOnly) { Write-Host "Protected local configuration ready: $directory";return }
if (-not $CopyCredentialOnly) {
    $socket=New-Object Net.Sockets.TcpClient
    try {
        if ($socket.ConnectAsync('127.0.0.1',8080).Wait(300)) { throw 'Port 8080 is already in use. If the test server is running, use a Copy credential command instead.' }
    } catch [AggregateException] { } finally { $socket.Dispose() }
}
Set-Clipboard -Value $credentials.$Account
Write-Host "Test credential for $Account copied to clipboard. Paste into GameLink Settings > Room service."
if ($CopyCredentialOnly) { return }
Write-Host 'Keep this console open while testing. Stop with Ctrl+C. Do not click Connect virtual network in this mock test.'
& $binary --mock --config $configFile
if ($LASTEXITCODE -ne 0) { throw 'Local test service stopped with an error. Review the console message above.' }
