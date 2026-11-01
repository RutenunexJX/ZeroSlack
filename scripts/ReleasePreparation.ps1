$ErrorActionPreference = 'Stop'

function Resolve-ZeroSlackGatePython([string]$Plan, [string]$Evidence, [string]$ExplicitPython) {
    if ($ExplicitPython) { return $ExplicitPython }
    if ($Plan) {
        return (Get-Content -LiteralPath $Plan -Raw | ConvertFrom-Json).defines.Python_EXECUTABLE
    }
    if ($Evidence) {
        return (Get-Content -LiteralPath $Evidence -Raw | ConvertFrom-Json).tools.python.path
    }
    return (Get-Command python -ErrorAction Stop).Source
}

function Invoke-ZeroSlackGate([string]$Python, [string[]]$Arguments) {
    & $Python (Join-Path $PSScriptRoot 'release_gate.py') @Arguments
    $childExitCode = $LASTEXITCODE
    if ($childExitCode -ne 0) { exit $childExitCode }
}
