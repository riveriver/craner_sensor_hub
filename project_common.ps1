# Version: 3.0.0
$ProjectScriptVersion = "3.0.0"

function Get-ProjectConfig {
    param([string]$ConfigPath)

    $defaults = @{
        Board = ""
        AppName = ""
        Address = ""
        Port = 1337
        ConnType = "udp"
        McuMgr = "mcumgr"
        ImagePath = ""
        FlashRunner = "stm32cubeprogrammer"
        FlashConnection = "port=SWD"
        FlashProgrammer = "STM32_Programmer_CLI"
        ZephyrBase = ""
        ZephyrSdkInstallDir = ""
        BuildDir = ""
        BootloaderHexPath = ""
        AppConfirmedHexPath = ""
        AppSignedBinPath = ""
        AppSignedHexPath = ""
        OtaOutputDir = ""
        OtaUpdateBinPath = ""
        OtaUpdateHexPath = ""
    }

    if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
        $ConfigPath = Join-Path $PSScriptRoot "project_config.json"
    } elseif (-not [System.IO.Path]::IsPathRooted($ConfigPath)) {
        $ConfigPath = Join-Path $PSScriptRoot $ConfigPath
    }

    if (Test-Path $ConfigPath) {
        try {
            $jsonConfig = Get-Content -Path $ConfigPath -Raw |
                ConvertFrom-Json
        } catch {
            Write-Error "Failed to read project JSON config: $ConfigPath. $($_.Exception.Message)"
            exit 1
        }

        Merge-ProjectConfigObject $defaults $jsonConfig
    }

    return $defaults
}

function Merge-ProjectConfigObject {
    param(
        [hashtable]$Config,
        [AllowNull()]$ConfigObject
    )

    if ($null -eq $ConfigObject) {
        return
    }

    foreach ($property in $ConfigObject.PSObject.Properties) {
        $key = $property.Name
        $value = $property.Value

        if ($Config.ContainsKey($key)) {
            $Config[$key] = $value
            continue
        }

        if ($null -ne $value -and $value -is [pscustomobject]) {
            Merge-ProjectConfigObject $Config $value
        }
    }
}

function Expand-ProjectConfigValue {
    param(
        [AllowNull()]$Value,
        [hashtable]$Config
    )

    if ($null -eq $Value) {
        return $Value
    }

    $expanded = [string]$Value
    $previous = $null
    while ($expanded -ne $previous) {
        $previous = $expanded
        foreach ($key in $Config.Keys) {
            $replacement = [string]$Config[$key]
            $expanded = $expanded.Replace("{$key}", $replacement)
        }
    }

    return $expanded
}

function Resolve-ProjectPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return Join-Path $PSScriptRoot $Path
}

function Require-ConfigValue {
    param(
        [string]$Name,
        [AllowNull()]$Value
    )

    if ($null -eq $Value -or ([string]$Value).Trim().Length -eq 0) {
        Write-Error "Missing required project_config.json value: $Name"
        exit 1
    }

    return $Value
}

function Use-ConfigValue {
    param(
        [AllowNull()]$Value,
        [AllowNull()]$ConfigValue
    )

    if ($null -eq $Value) {
        return $ConfigValue
    }

    if ($Value -is [string] -and [string]::IsNullOrWhiteSpace($Value)) {
        return $ConfigValue
    }

    return $Value
}
