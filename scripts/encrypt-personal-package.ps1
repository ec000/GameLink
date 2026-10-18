param([Parameter(Mandatory=$true)][string]$InputFile, [Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference = 'Stop'
$key = [Convert]::FromBase64String($env:GAMELINK_PERSONAL_OFFLINE_KEY)
if ($key.Length -ne 32) { throw 'Expected a private random 256-bit transport key' }
$header = [Text.Encoding]::ASCII.GetBytes('GLPKG001')
$nonce = [Security.Cryptography.RandomNumberGenerator]::GetBytes(12)
$tag = [byte[]]::new(16)
$plaintext = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($InputFile))
$ciphertext = [byte[]]::new($plaintext.Length)
$cipher = [Security.Cryptography.AesGcm]::new($key, 16)
try { $cipher.Encrypt($nonce, $plaintext, $ciphertext, $tag, $header) }
finally { $cipher.Dispose(); [Array]::Clear($key, 0, $key.Length); [Array]::Clear($plaintext, 0, $plaintext.Length) }
$destination = [IO.Path]::GetFullPath($OutputFile)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
$stream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew)
try {
    foreach ($part in @($header, $nonce, $tag, $ciphertext)) { $stream.Write($part, 0, $part.Length) }
} finally { $stream.Dispose() }
Write-Host 'Private installer encrypted with AES-256-GCM; only the ciphertext may be uploaded.'
