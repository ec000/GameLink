param([string]$ProcessName = '', [string]$Output = '.\gamelink-windows-report.json')
$ErrorActionPreference = 'Continue'
# Read-only; no firewall, routing, adapter priority or process changes.
$result = [ordered]@{
  generatedAt = [DateTime]::UtcNow.ToString('o')
  adapters = @(Get-NetAdapter | Select-Object Name, InterfaceDescription, InterfaceIndex, Status)
  interfaces = @(Get-NetIPInterface | Select-Object InterfaceIndex, InterfaceAlias, AddressFamily, InterfaceMetric, ConnectionState)
  routes = @(Get-NetRoute | Select-Object DestinationPrefix, NextHop, InterfaceIndex, RouteMetric)
  udp = @(Get-NetUDPEndpoint | Select-Object LocalAddress, LocalPort, OwningProcess)
  tcp = @(Get-NetTCPConnection | Select-Object LocalAddress, LocalPort, RemoteAddress, RemotePort, State, OwningProcess)
  processes = @()
  warning = 'UDP endpoints do not prove Steam/ZeroTier P2P path. No packet payloads collected.'
}
if ($ProcessName) { $result.processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object ProcessName, Id) }
$result | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $Output
Write-Output "Read-only report: $Output"
