$OtaScriptVersion = "2.0.0"

function Get-OtaConfig {
    param([string]$ConfigPath)

    $defaults = @{
        Board = "craner_general_stm32h743vit6"
        Address = "192.168.18.32"
        Port = 1337
        ConnType = "udp"
        McuMgr = "mcumgr"
        ImagePath = ""
    }

    if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
        $ConfigPath = Join-Path $PSScriptRoot "ota_config.json"
    } elseif (-not [System.IO.Path]::IsPathRooted($ConfigPath)) {
        $ConfigPath = Join-Path $PSScriptRoot $ConfigPath
    }

    if (Test-Path $ConfigPath) {
        try {
            $jsonConfig = Get-Content -Path $ConfigPath -Raw |
                ConvertFrom-Json
        } catch {
            Write-Error "Failed to read OTA JSON config: $ConfigPath. $($_.Exception.Message)"
            exit 1
        }

        foreach ($property in $jsonConfig.PSObject.Properties) {
            $key = $property.Name
            if ($defaults.ContainsKey($key)) {
                $defaults[$key] = $property.Value
            }
        }
    }

    return $defaults
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
