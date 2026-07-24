param(
    [string]$Config = "",
    [string]$Address = "",
    [Nullable[int]]$Port = $null,
    [string]$ConnType = "",
    [string]$McuMgr = "",
    [switch]$Version
)

$ScriptVersion = "2.0.0"
if ($Version) {
    Write-Host "image_list.ps1 version $ScriptVersion"
    exit 0
}

. "$PSScriptRoot\ota_common.ps1"

$otaConfig = Get-OtaConfig $Config
$Address = Use-ConfigValue $Address $otaConfig.Address
$Port = Use-ConfigValue $Port $otaConfig.Port
$ConnType = Use-ConfigValue $ConnType $otaConfig.ConnType
$McuMgr = Use-ConfigValue $McuMgr $otaConfig.McuMgr
$connString = "$Address`:$Port"

Write-Host "Querying MCUboot image list from $connString..."
& $McuMgr --conntype $ConnType --connstring $connString image list
exit $LASTEXITCODE
