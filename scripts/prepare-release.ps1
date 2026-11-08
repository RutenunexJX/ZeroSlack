[CmdletBinding(DefaultParameterSetName = 'Prepare')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Prepare')][string]$PreparationPlan,
    [Parameter(Mandatory = $true, ParameterSetName = 'Prepare')][string]$BuildDirectory,
    [Parameter(Mandatory = $true, ParameterSetName = 'Reuse')][string]$ReuseEvidence,
    [Parameter(Mandatory = $true, ParameterSetName = 'Reuse')][string]$VerificationOutput,
    [Parameter(ParameterSetName = 'Reuse')][string]$AcceptedPackage = '',
    [Parameter(ParameterSetName = 'Reuse')][string]$PackageSources = '',
    [string]$PythonExecutable = '',
    [switch]$Formal
)
. (Join-Path $PSScriptRoot 'ReleasePreparation.ps1')
$PreparationPlan = Resolve-ZeroSlackReleasePath $PreparationPlan
$ReuseEvidence = Resolve-ZeroSlackReleasePath $ReuseEvidence
$python = Resolve-ZeroSlackGatePython $PreparationPlan $ReuseEvidence $PythonExecutable
if ($PSCmdlet.ParameterSetName -eq 'Reuse') {
    $VerificationOutput = Resolve-ZeroSlackReleasePath $VerificationOutput
    $arguments = @('reuse', '--evidence', $ReuseEvidence, '--source', (Split-Path -Parent $PSScriptRoot), '--output', $VerificationOutput)
    if ($AcceptedPackage) { $arguments += @('--accepted-package', (Resolve-ZeroSlackReleasePath $AcceptedPackage)) }
    if ($PackageSources) { $arguments += @('--package-sources', (Resolve-ZeroSlackReleasePath $PackageSources)) }
    Invoke-ZeroSlackGate $python $arguments
    Write-Output $VerificationOutput
    return
}
$BuildDirectory = Resolve-ZeroSlackReleasePath $BuildDirectory
$arguments = @('prepare', '--plan', $PreparationPlan, '--work', $BuildDirectory)
if ($Formal) { $arguments += '--formal' }
Invoke-ZeroSlackGate $python $arguments
Write-Output (Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) 'preparation.json')
