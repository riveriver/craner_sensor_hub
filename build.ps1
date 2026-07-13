param(
    [string]$Board = "craner_general_stm32h743vit6",
    [string]$ExtraConf = "",
    [switch]$SkipOtaImages
)

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
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $SkipOtaImages) {
    $otaDir = Join-Path $buildDir "ota_images"
    $appZephyrDir = Join-Path $buildDir "craner_encoder_hub\zephyr"
    $signedBin = Join-Path $appZephyrDir "zephyr.signed.bin"
    $signedHex = Join-Path $appZephyrDir "zephyr.signed.hex"
    $confirmedBin = Join-Path $appZephyrDir "zephyr.signed.confirmed.bin"
    $confirmedHex = Join-Path $appZephyrDir "zephyr.signed.confirmed.hex"
    $updateBin = Join-Path $otaDir "app_update_signed.bin"
    $updateHex = Join-Path $otaDir "app_update_signed.hex"
    $initialBin = Join-Path $otaDir "app_initial_confirmed.bin"
    $initialHex = Join-Path $otaDir "app_initial_confirmed.hex"

    foreach ($path in @($signedBin, $signedHex, $confirmedBin, $confirmedHex)) {
        if (-not (Test-Path $path)) {
            Write-Error "Required MCUboot image not found: $path"
            exit 1
        }
    }

    New-Item -ItemType Directory -Force -Path $otaDir | Out-Null

    Copy-Item $signedBin $updateBin -Force
    Copy-Item $signedHex $updateHex -Force
    Copy-Item $confirmedBin $initialBin -Force
    Copy-Item $confirmedHex $initialHex -Force

    Write-Host "Generated MCUboot swap OTA images:"
    Write-Host "  $updateBin"
    Write-Host "  $initialHex"
}

exit 0
