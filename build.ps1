param(
    [string]$Board = "mini_stm32h743"
)

$workspaceRoot = Resolve-Path "$PSScriptRoot\.."
$env:ZEPHYR_BASE = Join-Path $workspaceRoot "zephyrproject\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = Join-Path $workspaceRoot "zephyr-sdk-1.0.1\zephyr-sdk-1.0.1"

$buildDir = Join-Path $PSScriptRoot "build\$Board"
$westArgs = @("build", "-b", $Board, $PSScriptRoot, "-d", $buildDir)

& python -m west @westArgs
