# Runs INSIDE the disposable guest only, from the separate setup CD.
# Do not invoke this script on the host. The three environment gates are required.
$ErrorActionPreference = 'Stop'
$computer = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
if ($env:COMPUTERNAME -ne 'RETSARLAB' -or
    $computer.Manufacturer -ne 'QEMU' -or
    $os.Caption -notmatch '^Microsoft Windows Server') {
    throw 'This bootstrap is restricted to the RETSARLAB QEMU Windows Server guest.'
}
if ($computer.TotalPhysicalMemory -gt 5GB -or $computer.NumberOfLogicalProcessors -gt 2) {
    throw 'Unexpected laboratory resource configuration.'
}

$serial = [IO.Ports.SerialPort]::new('COM1',115200,'None',8,'One')
$serial.WriteTimeout = 2000
$serial.Open()
function Write-GuestEvidence($value) {
    $serial.WriteLine(($value | ConvertTo-Json -Compress))
}
try {
    Write-GuestEvidence @{schema='retsar.guest-bootstrap.v1';phase='environment-verified';computer=$env:COMPUTERNAME;os=$os.Caption;createdSeat=$false}
    $feature = Get-WindowsFeature RDS-RD-Server
    if (!$feature.Installed) {
        $result = Install-WindowsFeature RDS-RD-Server -IncludeManagementTools
        Write-GuestEvidence @{phase='rdsh-role-install';success=[bool]$result.Success;restartNeeded=[string]$result.RestartNeeded;createdSeat=$false}
        if (!$result.Success) { throw 'Guest RDSH installation failed.' }
        if ([string]$result.RestartNeeded -eq 'Yes') {
            $guestWork = 'C:\RETSAR-Lab'
            New-Item -ItemType Directory -Path $guestWork -Force | Out-Null
            $guestScript = Join-Path $guestWork 'guest-bootstrap.ps1'
            if ($PSCommandPath -ine $guestScript) { Copy-Item -LiteralPath $PSCommandPath -Destination $guestScript }
            New-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce' -Name RETSARLabBootstrap -PropertyType String -Value 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\RETSAR-Lab\guest-bootstrap.ps1' -Force | Out-Null
            $serial.Close()
            Restart-Computer -Force
            exit
        }
    }
    # These settings are inside the gated disposable guest. No provider is registered.
    Set-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server' -Name fDenyTSConnections -Value 0
    Enable-NetFirewallRule -DisplayGroup 'Remote Desktop'
    $logon = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    Set-ItemProperty -LiteralPath $logon -Name AutoAdminLogon -Value '0'
    Remove-ItemProperty -LiteralPath $logon -Name DefaultPassword -ErrorAction SilentlyContinue
    $installed = [bool](Get-WindowsFeature RDS-RD-Server).Installed
    if (!$installed) { throw 'RDSH feature verification failed after configuration.' }
    $service = Get-Service TermService
    if ($service.Status -ne 'Running') { Start-Service TermService }
    $service.WaitForStatus('Running',[TimeSpan]::FromSeconds(30))
    $service.Refresh()
    # Configuration and a running service are not evidence of a usable provider/session.
    Write-GuestEvidence @{phase='rdsh-configured';installed=$installed;serviceStatus=[string]$service.Status;providerRegistered=$false;createdSeat=$false}
} catch {
    Write-GuestEvidence @{phase='failed';message=$_.Exception.Message;createdSeat=$false}
    throw
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
