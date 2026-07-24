param(
    [string]$Board = "craner_general_stm32h743vit6",
    [string]$ExtraConf = "",
    [switch]$Version
)

$ScriptVersion = "2.0.0"
if ($Version) {
    Write-Host "build.ps1 version $ScriptVersion"
    exit 0
}

$workspaceRoot = Resolve-Path "$PSScriptRoot\.."
$env:ZEPHYR_BASE = Join-Path $workspaceRoot "zephyrproject\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = Join-Path $workspaceRoot "zephyr-sdk-1.0.1\zephyr-sdk-1.0.1"

$buildDir = Join-Path $PSScriptRoot "build\$Board"
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
