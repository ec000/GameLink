param([Parameter(Mandatory=$true)][string]$StageDirectory)
$ErrorActionPreference='Stop'
$script=Join-Path $StageDirectory 'Start-LocalRoomTest.ps1'
$root=Join-Path $env:RUNNER_TEMP ('GameLink-local-demo-'+[Guid]::NewGuid())
& $script -DataRoot $root -PrepareOnly
$data=Join-Path $root 'LocalRoomTest'
$config=Get-Content (Join-Path $data 'server.local-mock.json') -Raw | ConvertFrom-Json
$credentials=Get-Content (Join-Path $data 'credentials.local-mock.json') -Raw | ConvertFrom-Json
if ($config.listen -ne '127.0.0.1:8080' -or $config.users.Count -ne 2 -or $config.controllerTokenFile -ne '') { throw 'Unexpected configuration' }
foreach ($name in @('alice','bob')) { if ($credentials.$name -notmatch '^[a-f0-9]{64}$') { throw 'Invalid random credential' } }
if ($credentials.alice -eq $credentials.bob) { throw 'Test accounts must have distinct credentials' }
$acl=Get-Acl -LiteralPath $data
if (-not $acl.AreAccessRulesProtected -or $acl.Access.Count -ne 2) { throw 'Test credentials directory ACL is not protected' }
$allowed=@([Security.Principal.WindowsIdentity]::GetCurrent().User.Value,'S-1-5-18')
foreach ($rule in $acl.Access) { if ($rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -notin $allowed) { throw 'Unexpected credential access' } }
$before=(Get-FileHash (Join-Path $data 'credentials.local-mock.json')).Hash
& $script -DataRoot $root -PrepareOnly
if ((Get-FileHash (Join-Path $data 'credentials.local-mock.json')).Hash -ne $before) { throw 'Restart replaced test credentials' }
& $script -DataRoot $root -CopyCredentialOnly -Account bob
if ((Get-Clipboard -Raw).Trim() -ne $credentials.bob) { throw 'Clipboard does not match selected test account' }
Set-Clipboard -Value 'GameLink local test'
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=(Resolve-Path (Join-Path $StageDirectory 'gamelink-server-local-demo.exe')).Path
$info.Arguments='--mock --config "'+(Join-Path $data 'server.local-mock.json')+'"'
$info.UseShellExecute=$false
$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($info)
try {
    $headers=@{Authorization='Bearer '+$credentials.alice}
    $healthy=$false
    for ($i=0;$i -lt 50;$i++) {
        try {
            $health=Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/v1/health' -Headers $headers -TimeoutSec 1
            if ($health.mode -eq 'mock') { $healthy=$true;break }
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $healthy) { throw 'Generated PowerShell credentials did not authenticate with actual Windows server' }
    $room=Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:8080/api/v1/rooms' -Headers $headers -ContentType 'application/json' -Body '{"gameId":"dst"}'
    if ($room.id -notmatch '^[a-f0-9]{16}$') { throw 'Cannot create room using generated credentials' }
    Invoke-RestMethod -Method Post -Uri ('http://127.0.0.1:8080/api/v1/rooms/'+$room.id+'/leave') -Headers $headers -ContentType 'application/json' -Body '{}' | Out-Null
} finally {
    if (-not $process.HasExited) { $process.Kill();$process.WaitForExit() }
    $process.Dispose()
}
Write-Host 'PASS: protected random credentials, loopback config, restart reuse, selected account clipboard.'
Write-Host 'PASS: actual Windows server authenticated generated credentials, created and left a room.'
