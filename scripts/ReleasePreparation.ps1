$ErrorActionPreference = 'Stop'

function Resolve-ZeroSlackReleasePath([string]$Value) {
    if (-not $Value) { return '' }
    if (-not [IO.Path]::IsPathRooted($Value)) { $Value = Join-Path (Split-Path -Parent $PSScriptRoot) $Value }
    return [IO.Path]::GetFullPath($Value)
}

function Resolve-ZeroSlackGatePython([string]$Plan, [string]$Evidence, [string]$ExplicitPython) {
    if ($ExplicitPython) { return (Resolve-ZeroSlackReleasePath $ExplicitPython) }
    if ($Plan) {
        return (Get-Content -LiteralPath $Plan -Raw | ConvertFrom-Json).defines.Python_EXECUTABLE
    }
    if ($Evidence) {
        $record = Get-Content -LiteralPath $Evidence -Raw | ConvertFrom-Json
        if ($record.schema -eq 'zeroslack.release-reuse/v1') {
            $record = Get-Content -LiteralPath $record.preparation -Raw | ConvertFrom-Json
        }
        return $record.tools.python.path
    }
    return (Get-Command python -ErrorAction Stop).Source
}

function Invoke-ZeroSlackGate([string]$Python, [string[]]$Arguments) {
    & $Python -B -X utf8 (Join-Path $PSScriptRoot 'release_gate.py') @Arguments
    $childExitCode = $LASTEXITCODE
    if ($childExitCode -ne 0) { exit $childExitCode }
}
