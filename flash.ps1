param(
    [string]$Board = "craner_general_board_v110",
    [string]$Runner = "stm32cubeprogrammer"
)

$workspaceRoot = Resolve-Path "$PSScriptRoot\.."
$env:ZEPHYR_BASE = Join-Path $workspaceRoot "zephyrproject\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = Join-Path $workspaceRoot "zephyr-sdk-1.0.1\zephyr-sdk-1.0.1"

$buildDir = Join-Path $PSScriptRoot "build\$Board"
$westArgs = @("flash", "-d", $buildDir, "--runner", $Runner)

& python -m west @westArgs
