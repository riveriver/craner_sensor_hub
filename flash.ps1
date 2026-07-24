param(
    [string]$Board = "craner_general_stm32h743vit6",
    [string]$Runner = "stm32cubeprogrammer",
    [ValidateSet("West", "Bootloader", "App", "All")]
    [string]$Target = "West",
    [string]$Connection = "port=SWD",
    [string]$Programmer = "STM32_Programmer_CLI",
    [switch]$IncludeBootloader,
    [switch]$DryRun,
    [switch]$Version
)

$ScriptVersion = "2.0.0"
if ($Version) {
    Write-Host "flash.ps1 version $ScriptVersion"
    exit 0
}

$workspaceRoot = Resolve-Path "$PSScriptRoot\.."
$env:ZEPHYR_BASE = Join-Path $workspaceRoot "zephyrproject\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = Join-Path $workspaceRoot "zephyr-sdk-1.0.1\zephyr-sdk-1.0.1"

$buildDir = Join-Path $PSScriptRoot "build\$Board"

if (-not (Test-Path $buildDir)) {
    Write-Error "Build directory not found: $buildDir. Run .\build.ps1 -Board $Board first."
    exit 1
}

function Require-File {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        Write-Error "Required image not found: $Path. Run .\build.ps1 -Board $Board first."
        exit 1
    }
}

function Invoke-Stm32Programmer {
    param(
        [string[]]$Arguments,
        [string]$Description
    )

    Write-Host $Description
    if ($DryRun) {
        Write-Host "$Programmer $($Arguments -join ' ')"
        return
    }

    & $Programmer @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Flash-Bootloader {
    $bootloaderHex = Join-Path $buildDir "mcuboot\zephyr\zephyr.hex"
    Require-File $bootloaderHex

    Invoke-Stm32Programmer `
        -Description "Flashing MCUboot bootloader to boot_partition..." `
        -Arguments @("-c", $Connection, "-w", $bootloaderHex, "-v")
}

function Flash-App {
    $appHex = Join-Path $buildDir "craner_encoder_hub\zephyr\zephyr.signed.confirmed.hex"
    Require-File $appHex

    Invoke-Stm32Programmer `
        -Description "Flashing confirmed application image to slot0_partition..." `
        -Arguments @("-c", $Connection, "-w", $appHex, "-v")
}

if ($Target -eq "West") {
    $westArgs = @("flash", "-d", $buildDir, "--runner", $Runner)

    if ($DryRun) {
        Write-Host "python -m west $($westArgs -join ' ')"
        exit 0
    }

    & python -m west @westArgs
    exit $LASTEXITCODE
}

if ($IncludeBootloader -or $Target -eq "Bootloader" -or $Target -eq "All") {
    Flash-Bootloader
}

switch ($Target) {
    "Bootloader" { break }
    "App" { Flash-App; break }
    "All" { Flash-App; break }
}

exit 0
