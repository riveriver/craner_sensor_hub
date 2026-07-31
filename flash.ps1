# Version: 3.0.0
param(
    [string]$Config = "",
    [string]$Board = "",
    [string]$Runner = "",
    [ValidateSet("West", "Bootloader", "App", "All")]
    [string]$Target = "West",
    [string]$Connection = "",
    [string]$Programmer = "",
    [switch]$IncludeBootloader,
    [switch]$DryRun,
    [switch]$Version
)

$ScriptVersion = "3.0.0"
if ($Version) {
    Write-Host "flash.ps1 version $ScriptVersion"
    exit 0
}

. "$PSScriptRoot\project_common.ps1"

$projectConfig = Get-ProjectConfig $Config
$Board = Use-ConfigValue $Board $projectConfig.Board
$Runner = Use-ConfigValue $Runner $projectConfig.FlashRunner
$Connection = Use-ConfigValue $Connection $projectConfig.FlashConnection
$Programmer = Use-ConfigValue $Programmer $projectConfig.FlashProgrammer

$Board = Require-ConfigValue "Board" $Board
$Runner = Require-ConfigValue "FlashRunner" $Runner
$Connection = Require-ConfigValue "FlashConnection" $Connection
$Programmer = Require-ConfigValue "FlashProgrammer" $Programmer

$env:ZEPHYR_BASE = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "ZephyrBase" $projectConfig.ZephyrBase) $projectConfig)
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "ZephyrSdkInstallDir" $projectConfig.ZephyrSdkInstallDir) $projectConfig)

$buildDir = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "BuildDir" $projectConfig.BuildDir) $projectConfig)

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
    $bootloaderHex = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "BootloaderHexPath" $projectConfig.BootloaderHexPath) $projectConfig)
    Require-File $bootloaderHex

    Invoke-Stm32Programmer `
        -Description "Flashing MCUboot bootloader to boot_partition..." `
        -Arguments @("-c", $Connection, "-w", $bootloaderHex, "-v")
}

function Flash-App {
    $appHex = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "AppConfirmedHexPath" $projectConfig.AppConfirmedHexPath) $projectConfig)
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
