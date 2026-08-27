# Version: 1.1.0
$toolScript = Join-Path $PSScriptRoot "zephyr-dev-workflow\script\workflow.ps1"
if (-not (Test-Path $toolScript)) {
    $toolScript = Join-Path $PSScriptRoot "tool\zephyr-dev-workflow\script\workflow.ps1"
}
$projectConfig = Join-Path $PSScriptRoot "project_config.json"

if (-not (Test-Path $toolScript)) {
    Write-Error "Workflow entry not found: $toolScript. Run git submodule update --init --recursive first."
    exit 1
}

$forwardArgs = @($args)
$hasConfig = $false
foreach ($arg in $forwardArgs) {
    if ($arg -eq "-config" -or $arg -like "-config:*") {
        $hasConfig = $true
        break
    }
}

if (-not $hasConfig -and $forwardArgs.Count -gt 0 -and -not ([string]$forwardArgs[0]).StartsWith("-")) {
    if ($forwardArgs.Count -gt 1) {
        $forwardArgs = @($forwardArgs[0], "-config", $projectConfig) + @($forwardArgs[1..($forwardArgs.Count - 1)])
    } else {
        $forwardArgs = @($forwardArgs[0], "-config", $projectConfig)
    }
}

& $toolScript @forwardArgs
exit $LASTEXITCODE
