[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$ReportPath,
    [string]$Objdump = 'E:/QT6/Tools/mingw1310_64/bin/objdump.exe'
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$info = Get-Content -LiteralPath (Join-Path $package 'build-info.json') -Raw | ConvertFrom-Json
if ($info.integratedFeatures.simdock.owner -ne 'zeroslack_core' -or
    $info.integratedFeatures.simdock.version -ne '0.6.1' -or
    $info.integratedFeatures.simdock.commit -ne 'e1747735735f0e50e06587d729784546efba56eb') {
    throw 'Missing or incorrect integrated SimDock source identity.'
}
if ($info.nativeComponents.PSObject.Properties.Name -contains 'simdock') { throw 'Retired SimDock DLL is still advertised.' }
if ($info.integratedFeatures.simdock.graphicalStimulus -ne $false -or
    $info.integratedFeatures.simdock.retiredWaveEditorSource.version -ne '0.15.2' -or
    $info.integratedFeatures.simdock.retiredWaveEditorSource.commit -ne '9730d475b02a59bec9e9c1ca8c6e0a508445dbca') { throw 'Retired graphical stimulus source identity mismatch.' }
if ($info.nativeComponents.PSObject.Properties.Name -contains 'wave') { throw 'Retired graphical stimulus component is still advertised.' }
$forbidden = @('SimDock.exe', 'simdock-workbench.dll', 'SimDockEla.dll', 'wavewidgets.dll', 'WaveWorkbenchEla.dll')
$allFiles = @(Get-ChildItem -LiteralPath $package -File -Recurse)
if (@($allFiles | Where-Object { $_.Name -in $forbidden }).Count) { throw 'Retired standalone runtime found in package.' }
foreach ($required in @('ZeroSlack.exe','zeroslack-cli.exe','libzeroslack_core.dll','libzeroslack_documents.dll',
    'libzeroslack_semantic.dll','ElaWidgetTools.dll','simdock-source.json',
    'licenses/simdock/LICENSE','licenses/simdock/ASSET-PROVENANCE.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) { throw "Missing package file: $required" }
}
$verified = 0
foreach ($line in (Get-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt'))) {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid checksum line.' }
    $expected = $Matches[1]
    $relative = $Matches[2]
    $file = [IO.Path]::GetFullPath((Join-Path $package $relative))
    if (-not $file.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Checksum path escaped the package.' }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "Checksum mismatch: $relative" }
    $verified++
}
if ($verified -ne $allFiles.Count - 1) { throw 'Checksum manifest does not cover the entire package.' }
$imports = [ordered]@{}
foreach ($name in @('ZeroSlack.exe','libzeroslack_core.dll','libzeroslack_documents.dll','libzeroslack_semantic.dll','ElaWidgetTools.dll')) {
    $dump = & $Objdump -p (Join-Path $package $name)
    if ($LASTEXITCODE -ne 0) { throw "PE inspection failed: $name" }
    $dependencies = @($dump | ForEach-Object { if ($_ -match 'DLL Name:\s*(.+)') { $Matches[1].Trim() } })
    if (@($dependencies | Where-Object { $_ -in $forbidden }).Count) { throw "Retired DLL import in $name" }
    $imports[$name] = $dependencies
}
$result = [ordered]@{schema='zeroslack.integrated-simdock-package-check/v1'; package=$package;
    passed=$true; verifiedFiles=$verified; integratedSource=$info.integratedFeatures.simdock;
    retiredWaveEditorSource=$info.integratedFeatures.simdock.retiredWaveEditorSource; imports=$imports;
    ownership='Simulation is owned by ZeroSlack; independent Tickx and its data are outside this package.'}
$parent = Split-Path -Parent ([IO.Path]::GetFullPath($ReportPath))
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ReportPath -Encoding utf8
$result | ConvertTo-Json -Depth 4
