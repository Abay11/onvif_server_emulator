#Requires -RunAsAdministrator
#Requires -Version 5.1
<#
.SYNOPSIS
Allow the ONVIF emulator through Windows and WSL Hyper-V firewalls.
.EXAMPLE
.\Configure-OnvifFirewall.ps1 -LanSubnet 192.168.1.0/24 -HostIPv4 192.168.1.10
.EXAMPLE
.\Configure-OnvifFirewall.ps1 -Remove
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$LanSubnet,
    [string]$HostIPv4,
    [ValidateRange(1, 65535)][int]$HttpPort = 8080,
    [ValidateRange(1, 65535)][int]$RtspPort = 8554,
    [ValidateRange(0, 65535)][int]$PullPointPort = 0,
    [ValidateRange(2, 65534)][int]$RtpPortMin = 50000,
    [ValidateRange(3, 65535)][int]$RtpPortMax = 50199,
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$creator = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'
$ruleNames = @('OnvifEmulator-TCP-In', 'OnvifEmulator-UDP-In', 'OnvifEmulator-UDP-Out')
foreach ($command in @('Get-NetFirewallHyperVRule', 'New-NetFirewallHyperVRule', 'Remove-NetFirewallHyperVRule')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw 'WSL Hyper-V firewall cmdlets are unavailable. Use an updated Windows 11 with WSL2 mirrored networking.'
    }
}
if (-not $Remove) {
    $parsedAddress = $null
    if ($LanSubnet -notmatch '^([0-9]{1,3}\.){3}[0-9]{1,3}/([0-9]|[12][0-9]|3[0-2])$' -or
        -not [System.Net.IPAddress]::TryParse(($LanSubnet -split '/')[0], [ref]$parsedAddress) -or
        $LanSubnet.EndsWith('/0')) {
        throw 'Supply the trusted IPv4 LAN subnet in CIDR form, for example 192.168.1.0/24 (not /0).'
    }
    if (-not [System.Net.IPAddress]::TryParse($HostIPv4, [ref]$parsedAddress) -or
        $parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork -or
        $HostIPv4 -eq '0.0.0.0') {
        throw 'Supply -HostIPv4 with the Windows LAN IPv4 used in ONVIF_ADVERTISED_IPV4.'
    }
    if ($RtpPortMin % 2 -ne 0 -or $RtpPortMax % 2 -ne 1 -or $RtpPortMin -ge $RtpPortMax) {
        throw 'RTP range must start at an even port and end at a larger odd port.'
    }
    $remoteAddresses = @($LanSubnet, $HostIPv4, '127.0.0.1')
    $tcpPorts = @("$HttpPort", "$RtspPort")
    if ($PullPointPort -ne 0) { $tcpPorts += "$PullPointPort" }
    $udpPorts = @('3702', "$RtpPortMin-$RtpPortMax")
}
# Replace only rules owned by this script. Default policies and unrelated rules are untouched.
foreach ($name in $ruleNames) {
    if ($PSCmdlet.ShouldProcess($name, $(if ($Remove) { 'Remove emulator firewall rules' } else { 'Replace emulator firewall rules' }))) {
        Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
        Get-NetFirewallHyperVRule -Name $name -ErrorAction SilentlyContinue | Remove-NetFirewallHyperVRule
        if (-not $Remove) {
            $direction = if ($name.EndsWith('-Out')) { 'Outbound' } else { 'Inbound' }
            $protocol = if ($name.Contains('-TCP-')) { 'TCP' } else { 'UDP' }
            $ports = if ($protocol -eq 'TCP') { $tcpPorts } else { $udpPorts }
            New-NetFirewallRule -Name $name -DisplayName $name -Group 'ONVIF Emulator' `
                -Direction $direction -Action Allow -Profile Private -Protocol $protocol `
                -LocalPort $ports -RemoteAddress $remoteAddresses | Out-Null
            New-NetFirewallHyperVRule -Name $name -DisplayName $name -VMCreatorId $creator `
                -Direction $direction -Action Allow -Profiles Private -Protocol $protocol `
                -LocalPorts $ports -RemoteAddresses $remoteAddresses | Out-Null
        }
    }
}
