# Version: 3.0.0
param(
    [string]$Config = "",
    [string]$Address = "",
    [Nullable[int]]$Port = $null,
    [string]$ConnType = "",
    [string]$McuMgr = "",
    [switch]$Version
)

$ScriptVersion = "3.0.0"
if ($Version) {
    Write-Host "image_list.ps1 version $ScriptVersion"
    exit 0
}

. "$PSScriptRoot\project_common.ps1"

$projectConfig = Get-ProjectConfig $Config
$Address = Use-ConfigValue $Address $projectConfig.Address
$Port = Use-ConfigValue $Port $projectConfig.Port
$ConnType = Use-ConfigValue $ConnType $projectConfig.ConnType
$McuMgr = Use-ConfigValue $McuMgr $projectConfig.McuMgr
$connString = "$Address`:$Port"

Write-Host "Querying MCUboot image list from $connString..."
& $McuMgr --conntype $ConnType --connstring $connString image list
exit $LASTEXITCODE
