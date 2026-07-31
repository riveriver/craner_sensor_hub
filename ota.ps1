# Version: 3.0.0
param(
    [string]$Config = "",
    [string]$Board = "",
    [string]$Address = "",
    [Nullable[int]]$Port = $null,
    [string]$ConnType = "",
    [string]$McuMgr = "",
    [string]$ImagePath = "",
    [switch]$SkipUpload,
    [switch]$SkipReset,
    [switch]$DryRun,
    [switch]$RawUploadOutput,
    [switch]$Version
)

$ScriptVersion = "3.0.0"
if ($Version) {
    Write-Host "ota.ps1 version $ScriptVersion"
    exit 0
}

. "$PSScriptRoot\project_common.ps1"

$projectConfig = Get-ProjectConfig $Config
$Board = Use-ConfigValue $Board $projectConfig.Board
$Address = Use-ConfigValue $Address $projectConfig.Address
$Port = Use-ConfigValue $Port $projectConfig.Port
$ConnType = Use-ConfigValue $ConnType $projectConfig.ConnType
$McuMgr = Use-ConfigValue $McuMgr $projectConfig.McuMgr
$ImagePath = Use-ConfigValue $ImagePath $projectConfig.ImagePath

$Board = Require-ConfigValue "Board" $Board
$Address = Require-ConfigValue "Address" $Address
$Port = Require-ConfigValue "Port" $Port
$ConnType = Require-ConfigValue "ConnType" $ConnType
$McuMgr = Require-ConfigValue "McuMgr" $McuMgr

$buildDir = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "BuildDir" $projectConfig.BuildDir) $projectConfig)
$otaDir = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "OtaOutputDir" $projectConfig.OtaOutputDir) $projectConfig)
$defaultSignedBin = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "AppSignedBinPath" $projectConfig.AppSignedBinPath) $projectConfig)
$defaultSignedHex = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "AppSignedHexPath" $projectConfig.AppSignedHexPath) $projectConfig)
$updateBin = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "OtaUpdateBinPath" $projectConfig.OtaUpdateBinPath) $projectConfig)
$updateHex = Resolve-ProjectPath (Expand-ProjectConfigValue (Require-ConfigValue "OtaUpdateHexPath" $projectConfig.OtaUpdateHexPath) $projectConfig)
$connString = "$Address`:$Port"

function Require-File {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        Write-Error "Required file not found: $Path. Run .\build.ps1 -Board $Board first."
        exit 1
    }
}

function Invoke-McuMgr {
    param([string[]]$Arguments)

    if ($DryRun) {
        Write-Host "$McuMgr $($Arguments -join ' ')"
        return @()
    }

    $output = & $McuMgr @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0) {
        exit $exitCode
    }

    return $output
}

function Invoke-McuMgrUpload {
    param([string[]]$Arguments)

    if ($DryRun) {
        Write-Host "$McuMgr $($Arguments -join ' ')"
        return
    }

    if ($RawUploadOutput) {
        & $McuMgr @Arguments
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        return
    }

    $output = & $McuMgr @Arguments 2>&1
    $exitCode = $LASTEXITCODE

    if ($exitCode -ne 0) {
        $output | ForEach-Object { Write-Host $_ }
        exit $exitCode
    }

    Write-Host "Upload complete."
}

function Get-Slot1Hash {
    param([string[]]$ImageListOutput)

    $inSlot1 = $false
    foreach ($line in $ImageListOutput) {
        if ($line -match "slot=1") {
            $inSlot1 = $true
            if ($line -match "hash[:=]\s*([0-9a-fA-F]+)") {
                return $Matches[1]
            }
            continue
        }

        if ($line -match "slot=0") {
            $inSlot1 = $false
        }

        if ($inSlot1 -and $line -match "hash[:=]\s*([0-9a-fA-F]+)") {
            return $Matches[1]
        }
    }

    return $null
}

if ([string]::IsNullOrWhiteSpace($ImagePath)) {
    Require-File $defaultSignedBin
    New-Item -ItemType Directory -Force -Path $otaDir | Out-Null
    Write-Host "Using sysbuild-generated signed image:"
    Write-Host "  $defaultSignedBin"
    Copy-Item $defaultSignedBin $updateBin -Force

    if (Test-Path $defaultSignedHex) {
        Copy-Item $defaultSignedHex $updateHex -Force
    }

    $ImagePath = $updateBin
    Write-Host "Prepared signed OTA image:"
    Write-Host "  $ImagePath"
} else {
    if (-not [System.IO.Path]::IsPathRooted($ImagePath)) {
        $ImagePath = Join-Path $PSScriptRoot $ImagePath
    }
    Require-File $ImagePath
}

if (-not $SkipUpload) {
    Write-Host "Uploading signed image to slot1..."
    Invoke-McuMgrUpload @("--conntype", $ConnType, "--connstring", $connString, "image", "upload", $ImagePath)
}

Write-Host "Reading image list..."
$imageList = Invoke-McuMgr @("--conntype", $ConnType, "--connstring", $connString, "image", "list")
if ($DryRun) {
    Write-Host "Dry run stops before parsing slot1 image hash."
    exit 0
}

$slot1Hash = Get-Slot1Hash $imageList
if ([string]::IsNullOrWhiteSpace($slot1Hash)) {
    Write-Error "Could not find slot1 image hash from mcumgr image list output."
    exit 1
}

if ($SkipUpload) {
    Write-Host "Current slot1 firmware hash: $slot1Hash"
} else {
    Write-Host "OTA update firmware hash: $slot1Hash"
}

Write-Host "Marking slot1 image as test upgrade: $slot1Hash"
Invoke-McuMgr @("--conntype", $ConnType, "--connstring", $connString, "image", "test", $slot1Hash) | Out-Null

if ($SkipReset) {
    Write-Host "OTA image marked as test upgrade. Reset skipped."
    exit 0
}

Write-Host "Resetting device to switch to the new firmware..."
Invoke-McuMgr @("--conntype", $ConnType, "--connstring", $connString, "reset") | Out-Null

Write-Host "OTA requested. After the new firmware boots and passes validation, run:"
Write-Host "  .\image_comfirm.ps1 -Address $Address -Port $Port"
exit 0
