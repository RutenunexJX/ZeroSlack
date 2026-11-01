[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PreparationPlan,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [string]$PythonExecutable = '',
    [switch]$Formal
)
. (Join-Path $PSScriptRoot 'ReleasePreparation.ps1')
$python = Resolve-ZeroSlackGatePython $PreparationPlan '' $PythonExecutable
$arguments = @('prepare', '--plan', $PreparationPlan, '--work', $BuildDirectory)
if ($Formal) { $arguments += '--formal' }
Invoke-ZeroSlackGate $python $arguments
Write-Output (Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) 'preparation.json')
