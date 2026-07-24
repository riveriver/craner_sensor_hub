param(
    [string]$Config = "",
    [string]$Address = "",
    [Nullable[int]]$Port = $null,
    [string]$ConnType = "",
    [string]$McuMgr = "",
    [switch]$DryRun,
    [switch]$Version
)

$ScriptVersion = "2.0.0"
if ($Version) {
    Write-Host "image_comfirm.ps1 version $ScriptVersion"
    exit 0
}

. "$PSScriptRoot\ota_common.ps1"

$otaConfig = Get-OtaConfig $Config
$Address = Use-ConfigValue $Address $otaConfig.Address
$Port = Use-ConfigValue $Port $otaConfig.Port
$ConnType = Use-ConfigValue $ConnType $otaConfig.ConnType
$McuMgr = Use-ConfigValue $McuMgr $otaConfig.McuMgr
$connString = "$Address`:$Port"

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

function Get-ActiveImageHash {
    param([string[]]$ImageListOutput)

    $currentHash = $null
    $currentIsActive = $false
    foreach ($line in $ImageListOutput) {
        if ($line -match "^\s*image=\d+\s+slot=\d+") {
            if ($currentIsActive -and -not [string]::IsNullOrWhiteSpace($currentHash)) {
                return $currentHash
            }

            $currentHash = $null
            $currentIsActive = $false
            continue
        }

        if ($line -match "^\s*hash:\s*([0-9a-fA-F]+)") {
            $currentHash = $Matches[1]
            continue
        }

        if ($line -match "^\s*flags:\s*(.*)$") {
            $flags = $Matches[1]
            if ($flags -match "(^|\s|,)active(\s|,|$)") {
                $currentIsActive = $true
            }
        }
    }

    if ($currentIsActive -and -not [string]::IsNullOrWhiteSpace($currentHash)) {
        return $currentHash
    }

    return $null
}

Write-Host "Reading image list from $connString..."
$imageList = Invoke-McuMgr @("--conntype", $ConnType, "--connstring", $connString, "image", "list")
if ($DryRun) {
    Write-Host "Dry run stops before parsing active image hash."
    exit 0
}

$activeHash = Get-ActiveImageHash $imageList
if ([string]::IsNullOrWhiteSpace($activeHash)) {
    Write-Error "Could not find active image hash from mcumgr image list output."
    exit 1
}

$argsList = @("--conntype", $ConnType, "--connstring", $connString, "image", "confirm", $activeHash)

Write-Host "Confirming active firmware as valid: $activeHash"
Invoke-McuMgr $argsList | Out-Null
exit 0
