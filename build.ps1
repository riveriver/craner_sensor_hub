# Version: 3.0.0
param(
    [string]$Config = "",
    [string]$Board = "",
    [string]$ExtraConf = "",
    [switch]$Version
)

$ScriptVersion = "3.0.0"
if ($Version) {
    Write-Host "build.ps1 version $ScriptVersion"
    exit 0
}

. "$PSScriptRoot\project_common.ps1"

$projectConfig = Get-ProjectConfig $Config
$Board = Use-ConfigValue $Board $projectConfig.Board
$Board = Require-ConfigValue "Board" $Board

$env:ZEPHYR_BASE = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "ZephyrBase" $projectConfig.ZephyrBase) $projectConfig)
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "ZephyrSdkInstallDir" $projectConfig.ZephyrSdkInstallDir) $projectConfig)

$buildDir = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "BuildDir" $projectConfig.BuildDir) $projectConfig)
$westArgs = @(
    "build", "--sysbuild",
    "-p", "always",
    "-b", $Board,
    $PSScriptRoot,
    "-d", $buildDir,
    "--",
    "-DBOARD_ROOT=$PSScriptRoot"
)

if (-not [string]::IsNullOrWhiteSpace($ExtraConf)) {
    $extraConfPath = if ([System.IO.Path]::IsPathRooted($ExtraConf)) {
        $ExtraConf
    } else {
        Join-Path $PSScriptRoot $ExtraConf
    }

    if (-not (Test-Path $extraConfPath)) {
        Write-Error "Extra config file not found: $extraConfPath"
        exit 1
    }

    $westArgs += "-DEXTRA_CONF_FILE=$extraConfPath"
}

& python -m west @westArgs
exit $LASTEXITCODE
