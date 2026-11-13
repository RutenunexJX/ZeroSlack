[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/ela-migration',
    [string]$OutputDirectory = '',
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64',
    [string]$XipsPackageDirectory = '',
    [string]$XipsAssetAdapterDirectory = '',
    [string]$DocumentEditorDirectory = '',
    [string]$PreparationPlan = '',
    [string]$PreparationEvidence = '',
    [string]$PythonExecutable = '',
    [switch]$Formal,
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ReleasePreparation.ps1')
$PreparationPlan = Resolve-ZeroSlackReleasePath $PreparationPlan
$PreparationEvidence = Resolve-ZeroSlackReleasePath $PreparationEvidence
$BuildDirectory = Resolve-ZeroSlackReleasePath $BuildDirectory
$OutputDirectory = Resolve-ZeroSlackReleasePath $OutputDirectory
foreach ($parameter in @('QtDirectory','CompilerDirectory','XipsPackageDirectory','XipsAssetAdapterDirectory','DocumentEditorDirectory')) {
    Set-Variable -Name $parameter -Value (Resolve-ZeroSlackReleasePath (Get-Variable -Name $parameter -ValueOnly))
}
$assemblySources = $null
$originalSourceRoot = $sourceRoot
if (-not $PreparationEvidence -and -not $PreparationPlan) {
    throw 'A preparation plan or complete preparation evidence is required before any package write.'
}
$python = Resolve-ZeroSlackGatePython $PreparationPlan $PreparationEvidence $PythonExecutable
if (-not $PreparationEvidence) {
    if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $sourceRoot $BuildDirectory }
    $arguments = @('prepare', '--plan', $PreparationPlan, '--work', $BuildDirectory)
    if ($Formal) { $arguments += '--formal' }
    Invoke-ZeroSlackGate $python $arguments
    $PreparationEvidence = Join-Path $BuildDirectory 'preparation.json'
}
$arguments = @('verify', '--evidence', $PreparationEvidence, '--source', $originalSourceRoot)
if ($Formal) { $arguments += '--formal' }
Invoke-ZeroSlackGate $python $arguments
$preparation = Get-Content -LiteralPath $PreparationEvidence -Raw | ConvertFrom-Json
if ($preparation.schema -eq 'zeroslack.release-reuse/v1') {
    if (-not $OutputDirectory) { throw 'Reused candidates require an explicit new staging directory.' }
    if ($PreparationPlan -or $PSBoundParameters.ContainsKey('BuildDirectory') -or $XipsPackageDirectory -or
        $XipsAssetAdapterDirectory -or $DocumentEditorDirectory) {
        throw 'Reuse staging accepts only the verified candidate and retained package; input overrides require new evidence.'
    }
    $original = Get-Content -LiteralPath $preparation.preparation -Raw | ConvertFrom-Json
    if ([IO.Path]::GetFullPath($QtDirectory) -ne [IO.Path]::GetFullPath($original.qtDirectory) -or
        [IO.Path]::GetFullPath($CompilerDirectory) -ne [IO.Path]::GetFullPath($original.compilerDirectory)) {
        throw 'Reuse packaging toolchain differs from the verified candidate.'
    }
    if ($preparation.acceptedPackage) {
        $arguments = @('stage-reused-package', '--evidence', $PreparationEvidence, '--package', $OutputDirectory)
        if ($AllowDirty) { $arguments += '--allow-dirty' }
        Invoke-ZeroSlackGate $python $arguments
        Write-Output $OutputDirectory
        return
    }
    if (-not $preparation.packageSources) { throw 'First packaging requires a verified PackageSources plan.' }
    $assemblySources = Get-Content -LiteralPath $preparation.packageSources.path -Raw | ConvertFrom-Json
    $preparation = $original
    $XipsPackageDirectory = $assemblySources.sources.native.root
    $XipsAssetAdapterDirectory = $assemblySources.sources.adapter.root
    $DocumentEditorDirectory = $assemblySources.sources.documents.root
}
$gitExecutable = $preparation.tools.git.path
if (-not $gitExecutable) { throw 'Verified preparation must identify its Git executable.' }
if ($PSBoundParameters.ContainsKey('BuildDirectory') -and -not $PreparationPlan -and
    [IO.Path]::GetFullPath($BuildDirectory) -ne [IO.Path]::GetFullPath($preparation.buildDirectory) -and
    [IO.Path]::GetFullPath($BuildDirectory) -ne [IO.Path]::GetFullPath($preparation.work)) {
    throw 'BuildDirectory does not identify the prepared candidate.'
}
if ([IO.Path]::GetFullPath($QtDirectory) -ne [IO.Path]::GetFullPath($preparation.qtDirectory) -or
    [IO.Path]::GetFullPath($CompilerDirectory) -ne [IO.Path]::GetFullPath($preparation.compilerDirectory)) {
    throw 'Packaging toolchain differs from the prepared candidate.'
}
foreach ($override in @(
    @{ value=$XipsPackageDirectory; expected=(Join-Path $preparation.buildDirectory 'components/xips') },
    @{ value=$XipsAssetAdapterDirectory; expected=(Join-Path $preparation.buildDirectory 'components/xips-asset-adapter') },
    @{ value=$DocumentEditorDirectory; expected=(Join-Path $preparation.buildDirectory 'resources/document-editors') }
)) {
    if (-not $assemblySources -and $override.value -and [IO.Path]::GetFullPath($override.value) -ne [IO.Path]::GetFullPath($override.expected)) {
        throw 'Packaging resource override differs from the prepared candidate; include it in a new preparation plan.'
    }
}
$sourceRoot = $preparation.sourceRoot
$deliverySourceRoot = if ($assemblySources) { $originalSourceRoot } else { $sourceRoot }
$BuildDirectory = $preparation.buildDirectory
$version = (Get-Content -LiteralPath (Join-Path $sourceRoot 'VERSION') -Raw).Trim()
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $sourceRoot $BuildDirectory }
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
if ($cache -notcontains 'ZEROSLACK_ENABLE_ELA:BOOL=ON') { throw 'The maintained release requires the Ela backend.' }
$generatedVersion = [regex]::Match(
    (Get-Content -LiteralPath (Join-Path $buildRoot 'generated/version.h') -Raw),
    '#define\s+APP_VERSION\s+"([^"]+)"')
if (-not $generatedVersion.Success -or $generatedVersion.Groups[1].Value -ne $version) {
    throw 'Generated application version does not match VERSION; reconfigure and rebuild first.'
}
$suiteBuildPath = Join-Path $buildRoot 'generated/suiteapp-build.json'
if (-not (Test-Path -LiteralPath $suiteBuildPath -PathType Leaf)) {
    throw 'SuiteApp build metadata is missing; reconfigure and rebuild first.'
}
$suiteBuild = Get-Content -LiteralPath $suiteBuildPath -Raw | ConvertFrom-Json
$revision = & $gitExecutable -C $originalSourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git revision.' }
$branch = & $gitExecutable -C $originalSourceRoot branch --show-current
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git branch.' }
$sourceStatus = & $gitExecutable -c core.fsmonitor=false -C $originalSourceRoot status --porcelain --untracked-files=no --ignore-submodules=all
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git source status.' }
$trackedNames = @(& $gitExecutable -C $originalSourceRoot ls-files)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read tracked candidate paths.' }
$preparedSources = Get-Content -LiteralPath $preparation.sourceManifest -Raw | ConvertFrom-Json
$untrackedPreparedFiles = @($preparedSources.files.PSObject.Properties.Name | Where-Object { $_ -notin $trackedNames -and $_ -notlike 'thirdparty/*' })
$dirty = [bool]$sourceStatus -or $untrackedPreparedFiles.Count -gt 0
if ($Formal -and $dirty -and -not $AllowDirty) {
    throw 'Formal packages require clean source unless -AllowDirty is explicitly requested.'
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $originalSourceRoot 'build/packages/ZeroSlack-win64' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
# Verify the new staging directory before replacing the installed package.
if (Test-Path -LiteralPath $outputRoot) { throw 'Output must be a new directory.' }
$binaries = @('ZeroSlack.exe', 'zeroslack-cli.exe', 'libzeroslack_core.dll',
    'libzeroslack_semantic.dll', 'libzeroslack_documents.dll', 'ElaWidgetTools.dll', 'WebView2Loader.dll')
foreach ($name in $binaries) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $name) -PathType Leaf)) { throw "Missing binary: $name" }
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
foreach ($name in $binaries) { Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $outputRoot }
$deployTargets = @($binaries | ForEach-Object { Join-Path $outputRoot $_ })
$nativeComponents = [ordered]@{}
$nativeHostRuntimeFiles = @()
$nativeHostPluginFiles = @()
function Join-NativeRuntimePath([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        $Relative -match '(^|[\\/])\.\.([\\/]|$)' -or $Relative.Contains(':')) {
        throw "Invalid native runtime path: $Relative"
    }
    return Join-Path $Root $Relative
}
foreach ($component in @(
    @{ id='xips'; package=$XipsPackageDirectory; setting='ZEROSLACK_XIPS_COMPONENT_DIR'; files=@('xips-browser.dll','xips-browser-impl.dll','XipsEla.dll') }
)) {
    $configured = $cache | Where-Object { $_ -match "^$($component.setting):[^=]+=(.+)$" }
    $componentRoot = if ($component.package) {
        (Resolve-Path -LiteralPath $component.package).Path
    } else { Join-Path $buildRoot "components/$($component.id)" }
    if (-not (Test-Path -LiteralPath $componentRoot -PathType Container)) {
        if ($configured) { throw "Configured native component is missing: $componentRoot" }
        continue
    }
    $componentFiles = @($component.files)
    $runtimeManifestPath = Join-Path $componentRoot 'xips-native-runtime.json'
    $runtimeManifest = $null
    if (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf) {
        $runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
        if ($runtimeManifest.schema -ne 'xips.native-deployment/v1' -or
            $runtimeManifest.nativeSurfaceAbi -ne 1 -or $runtimeManifest.qt -ne '6.10.2') {
            throw 'The xIPs runtime manifest is incompatible with this host.'
        }
        $componentFiles = @($componentFiles + @($runtimeManifest.componentFiles) | Select-Object -Unique)
        $nativeHostRuntimeFiles += @($runtimeManifest.additionalHostRuntimeFiles)
        $nativeHostPluginFiles += @($runtimeManifest.hostPluginFiles)
    }
    $componentTarget = Join-Path $outputRoot "components/$($component.id)"
    New-Item -ItemType Directory -Path $componentTarget | Out-Null
    foreach ($name in $componentFiles) {
        $source = Join-NativeRuntimePath $componentRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing native runtime: $source" }
        $target = Join-NativeRuntimePath $componentTarget $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        if ($name.EndsWith('.dll')) { $deployTargets += $target }
    }
    $componentInfo = $null
    $infoPath = Join-Path $componentRoot 'build-info.json'
    if (Test-Path -LiteralPath $infoPath -PathType Leaf) {
        $componentInfo = Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json
        if ($Formal -and ($componentInfo.channel -ne 'formal' -or $componentInfo.dirty -or $componentInfo.sourceDirty)) {
            throw "Native component package is not a clean formal release: $componentRoot"
        }
        Copy-Item -LiteralPath $infoPath -Destination $componentTarget
    } elseif ($Formal -and $component.package) {
        throw "Native component package has no build-info.json: $componentRoot"
    }
    foreach ($name in @('xips-capabilities.json')) {
        $path = Join-Path $componentRoot $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { Copy-Item -LiteralPath $path -Destination $componentTarget }
    }
    $componentNotices = Join-Path $outputRoot "licenses/components/$($component.id)"
    if ($assemblySources) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $componentNotices) -Force | Out-Null
        Copy-Item -LiteralPath $assemblySources.sources.nativeLicenses.root -Destination $componentNotices -Recurse
    }
    foreach ($name in @('licenses', 'vendor', 'THIRD-PARTY-NOTICES.md')) {
        $path = Join-Path $componentRoot $name
        if (Test-Path -LiteralPath $path) {
            New-Item -ItemType Directory -Path $componentNotices -Force | Out-Null
            if ($name -eq 'licenses') {
                Get-ChildItem -LiteralPath $path | ForEach-Object {
                    Copy-Item -LiteralPath $_.FullName -Destination $componentNotices -Recurse
                }
            } else { Copy-Item -LiteralPath $path -Destination $componentNotices -Recurse }
        }
    }
    # An already packaged component stores notices under the package owner.
    # Preserve its matching notices without consulting a newer standalone app.
    $parentNotices = Join-Path (Split-Path -Parent (Split-Path -Parent $componentRoot)) "licenses/components/$($component.id)"
    if (-not (Test-Path -LiteralPath $componentNotices) -and (Test-Path -LiteralPath $parentNotices)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $componentNotices) -Force | Out-Null
        Copy-Item -LiteralPath $parentNotices -Destination $componentNotices -Recurse
    }
    if ($runtimeManifest) {
        foreach ($name in @($runtimeManifest.helperLicenseFiles)) {
            $relative = $name -replace '^licenses[\\/]', ''
            $notice = Join-NativeRuntimePath $componentNotices $relative
            if (-not (Test-Path -LiteralPath $notice -PathType Leaf)) { throw "Missing native helper notice: $notice" }
        }
    }
    $nativeComponents[$component.id] = [ordered]@{
        directory="components/$($component.id)"; files=$componentFiles
        version=$(if ($componentInfo) { $componentInfo.version } else { $null })
        revision=$(if ($componentInfo.revision) { $componentInfo.revision } elseif ($componentInfo.sourceCommit) { $componentInfo.sourceCommit } else { $null })
    }
}
# Wave blocks load the accepted Tickx memory editor from the application directory.
$tickxRoot = Join-Path $buildRoot 'components/tickx'
$tickxTarget = Join-Path $outputRoot 'components/tickx'
$tickxFiles = @('wavewidgets.dll','WaveWorkbenchEla.dll','LICENSE','NOTICE','THIRD-PARTY-NOTICES.md','build-info.json')
New-Item -ItemType Directory -Path $tickxTarget -Force | Out-Null
foreach ($name in $tickxFiles) {
    $inputPath = Join-Path $tickxRoot $name
    $relative = "components/tickx/$name"
    $proof = $preparation.packageInputs.PSObject.Properties[$relative]
    if (-not $proof -or -not (Test-Path -LiteralPath $inputPath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $proof.Value) {
        throw "Missing or changed accepted Wave block runtime: $relative"
    }
    Copy-Item -LiteralPath $inputPath -Destination (Join-Path $tickxTarget $name)
}
$tickxInfo = Get-Content -LiteralPath (Join-Path $tickxRoot 'build-info.json') -Raw | ConvertFrom-Json
$nativeComponents['tickx'] = [ordered]@{
    directory='components/tickx'; files=$tickxFiles; version=$tickxInfo.version; revision=$tickxInfo.revision
}
$deployTargets += @(Join-Path $tickxTarget 'wavewidgets.dll')
$deployTargets += @(Join-Path $tickxTarget 'WaveWorkbenchEla.dll')

# Keep the asset adapter in its own process/runtime; its Qt DLLs are not host DLLs.
$adapterRoot = if ($XipsAssetAdapterDirectory) {
    (Resolve-Path -LiteralPath $XipsAssetAdapterDirectory).Path
} else { Join-Path $buildRoot 'components/xips-asset-adapter' }
$assetAdapter = $null
if (-not (Test-Path -LiteralPath $adapterRoot -PathType Container)) {
    if ($Formal) { throw 'The formal package requires the independent xIPs asset adapter runtime.' }
} else {
    $adapterInfo = Get-Content -LiteralPath (Join-Path $adapterRoot 'build-info.json') -Raw | ConvertFrom-Json
    if ($adapterInfo.schema -ne 'xips.asset-adapter.build/v1' -or $adapterInfo.protocolVersion -ne 1 -or
        $adapterInfo.buildType -ne 'Release' -or $adapterInfo.qt -ne '6.10.2' -or
        (-not $assemblySources -and ($adapterInfo.coreVersion -ne $nativeComponents.xips.version -or
        $adapterInfo.coreCommit -ne $nativeComponents.xips.revision))) {
        throw 'The asset adapter identity does not match the native xIPs component.'
    }
    foreach ($entry in @(
        @{ name='xips-asset-adapter.exe'; hash=$adapterInfo.executableSha256 },
        @{ name='ADAPTER-PROTOCOL.json'; hash=$adapterInfo.protocolSha256 }
    )) {
        if ((Get-FileHash -LiteralPath (Join-Path $adapterRoot $entry.name)).Hash.ToLowerInvariant() -ne $entry.hash) {
            throw "Asset adapter identity mismatch: $($entry.name)"
        }
    }
    $adapterFiles = @('xips-asset-adapter.exe','xips-cli.exe','Qt6Core.dll','Qt6Sql.dll',
        'libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll','sqldrivers/qsqlite.dll',
        'qt.conf','build-info.json','ADAPTER-PROTOCOL.json','README.txt','LICENSE','NOTICE',
        'licenses/ADAPTER-NOTICE.txt','licenses/GCC-COPYING.RUNTIME.txt','licenses/GCC-COPYING3.LIB.txt',
        'licenses/GCC-COPYING3.txt','licenses/MinGW-w64-COPYING.txt','licenses/Qt-LGPLv3.txt',
        'licenses/winpthreads-COPYING.txt')
    $adapterTarget = Join-Path $outputRoot 'components/xips-asset-adapter'
    foreach ($name in $adapterFiles) {
        $source = Join-NativeRuntimePath $adapterRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing asset adapter runtime: $name" }
        $target = Join-NativeRuntimePath $adapterTarget $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
    $assetAdapter = [ordered]@{ directory='components/xips-asset-adapter'; files=$adapterFiles;
        artifactId=$adapterInfo.artifactId; protocolVersion=$adapterInfo.protocolVersion;
        coreVersion=$adapterInfo.coreVersion; coreCommit=$adapterInfo.coreCommit;
        adapterSourceSha256=$adapterInfo.adapterSourceSha256; executableSha256=$adapterInfo.executableSha256 }
}
$integratedSimDock = Get-Content -LiteralPath (Join-Path $buildRoot 'simdock-source.json') -Raw | ConvertFrom-Json
if ($integratedSimDock.version -ne '0.6.1' -or $integratedSimDock.commit -ne 'e1747735735f0e50e06587d729784546efba56eb' -or $integratedSimDock.owner -ne 'zeroslack_core') {
    throw 'Integrated SimDock source provenance does not match the reviewed boundary.'
}
Copy-Item -LiteralPath (Join-Path $buildRoot 'simdock-source.json') -Destination $outputRoot
& (Join-Path $QtDirectory 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-opengl-sw --dir $outputRoot @deployTargets
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
foreach ($name in @($nativeHostRuntimeFiles | Select-Object -Unique)) {
    $source = Join-NativeRuntimePath (Join-Path $QtDirectory 'bin') $name
    $target = Join-NativeRuntimePath $outputRoot $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}
foreach ($name in @($nativeHostPluginFiles | Select-Object -Unique)) {
    $source = Join-NativeRuntimePath (Join-Path $QtDirectory 'plugins') $name
    $target = Join-NativeRuntimePath $outputRoot $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}
if ($nativeComponents.Contains('xips')) {
    $sqlDrivers = Join-Path $outputRoot 'sqldrivers'
    New-Item -ItemType Directory -Path $sqlDrivers -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $QtDirectory 'bin/Qt6Sql.dll') -Destination $outputRoot -Force
    Copy-Item -LiteralPath (Join-Path $QtDirectory 'plugins/sqldrivers/qsqlite.dll') -Destination $sqlDrivers -Force
}
foreach ($name in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory "bin/$name") -Destination $outputRoot
}
$licenseRoot = Join-Path $outputRoot 'licenses'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'resources/licenses/simdock') -Destination $licenseRoot -Recurse
$behaviorNotices = Join-Path $licenseRoot 'simulation-behaviors'
New-Item -ItemType Directory -Path $behaviorNotices -Force | Out-Null
foreach ($name in @('LICENSE','NOTICE','THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot "resources/simulation/tb-behaviors/$name") -Destination $behaviorNotices
}
$licenses = [ordered]@{
    'slang-MIT.txt' = 'thirdparty/slang/LICENSE'
    'tree-sitter-MIT.txt' = 'thirdparty/tree_sitter/LICENSE'
    'tree-sitter-systemverilog-MIT.txt' = 'thirdparty/tree_sitter_systemverilog/LICENSE'
    'tree-sitter-ICU.txt' = 'thirdparty/tree_sitter/lib/src/unicode/LICENSE'
    '0xProto-OFL.txt' = 'resources/fonts/0xproto/LICENSE.txt'
    'GeistMono-OFL.txt' = 'resources/fonts/geist-mono/LICENSE.txt'
    'IntelOneMono-OFL.txt' = 'resources/fonts/intel-one-mono/LICENSE.txt'
    'Iosevka-OFL.txt' = 'resources/fonts/iosevka/LICENSE.txt'
    'MapleMono-OFL.txt' = 'resources/fonts/maple-mono/LICENSE.txt'
    'MonaspaceNeon-OFL.txt' = 'resources/fonts/monaspace-neon/LICENSE.txt'
    'Catppuccin-MIT.txt' = 'resources/catppuccin/LICENSE.txt'
}
foreach ($entry in $licenses.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $entry.Value) -Destination (Join-Path $licenseRoot $entry.Key)
}
$elaLicenses = Join-Path $licenseRoot 'ElaWidgetTools'
New-Item -ItemType Directory -Path $elaLicenses | Out-Null
foreach ($name in @('LICENSE', 'UPSTREAM-REVISION.md', 'UPSTREAM-README.md', 'Font/FontAwesome-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot "thirdparty/elawidgettools/$name") -Destination $elaLicenses
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'thirdparty/elawidgettools/patches') -Destination $elaLicenses -Recurse
foreach ($name in @('COPYING3', 'COPYING3.LIB', 'COPYING.RUNTIME')) {
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory "licenses/gcc/$name") -Destination (Join-Path $licenseRoot "GCC-$name.txt")
}
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/gcc/COPYING3.LIB') -Destination (Join-Path $licenseRoot 'Qt-LGPLv3.txt')
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/winpthreads/COPYING') -Destination (Join-Path $licenseRoot 'winpthreads-COPYING.txt')
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/mingw-w64/COPYING.MinGW-w64.txt') -Destination (Join-Path $licenseRoot 'MinGW-w64-COPYING.txt')
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $outputRoot
}
Copy-Item -LiteralPath (Join-Path $deliverySourceRoot '用户手册.md') -Destination $outputRoot
# Ship editable user-template definitions and their non-destructive merge tool.
$templateTarget = Join-Path $outputRoot 'templates'
New-Item -ItemType Directory -Path $templateTarget -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'resources/templates') |
    Where-Object { $_.Name -ne '__pycache__' } |
    Copy-Item -Destination $templateTarget -Recurse
$editorSource = if ($DocumentEditorDirectory) { (Resolve-Path -LiteralPath $DocumentEditorDirectory).Path } else { Join-Path $buildRoot 'resources/document-editors' }
$editorSource = [IO.Path]::GetFullPath($editorSource).TrimEnd('\','/')
if ($outputRoot.Equals($editorSource, [StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.StartsWith($editorSource + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must be outside the document editor bundle.'
}
$editorManifestPath = Join-Path $editorSource 'manifest.json'
$editorManifest = Get-Content -LiteralPath $editorManifestPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($editorManifest.format -ne 'zeroslack.document-editors' -or $editorManifest.version -ne 1) { throw 'Build the pinned document editor resources before packaging.' }
foreach ($name in @('WebView2Loader.dll')) {
    if (-not $editorManifest.runtimeFiles.$name -or
        (Get-FileHash -LiteralPath (Join-Path $buildRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $editorManifest.runtimeFiles.$name) {
        throw "Document editor loader checksum mismatch: $name"
    }
}
foreach ($entry in $editorManifest.files.PSObject.Properties) {
    $resource = Join-NativeRuntimePath $editorSource $entry.Name
    if ((Get-FileHash -LiteralPath $resource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
        throw "Document editor checksum mismatch: $($entry.Name)"
    }
}
foreach ($required in @('markdown/index.html','blocks/index.html','NOTICE.txt','licenses/MarkText-Muya-MIT.txt','licenses/BlockSuite-MPL-2.0.txt','licenses/WebView2-SDK-LICENSE.txt','sources/build-document-editors.py')) {
    if (-not $editorManifest.files.PSObject.Properties[$required]) { throw "Incomplete document editor bundle: $required" }
}
Copy-Item -LiteralPath $editorSource -Destination (Join-Path $outputRoot 'resources/document-editors') -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'thirdparty/document-editors/README.txt') -Destination (Join-Path $outputRoot 'DOCUMENT-EDITORS.txt')
Copy-Item -LiteralPath (Join-Path $deliverySourceRoot 'packaging/ZeroSlack-PACKAGE-README.txt') -Destination (Join-Path $outputRoot 'README.txt')
# Public CLI documentation and the headless Qt platform are runtime payload.
New-Item -ItemType Directory -Path (Join-Path $outputRoot 'docs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $deliverySourceRoot 'docs/cli-automation.md') -Destination (Join-Path $outputRoot 'docs/cli-automation.md')
Copy-Item -LiteralPath (Join-Path $QtDirectory 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $outputRoot 'platforms/qoffscreen.dll')
$channel = if ($Formal) { 'formal' } else { 'preview' }
$releaseTag = $null
if ($Formal -and -not $dirty) {
    $matchingTags = @(& $gitExecutable -C $originalSourceRoot tag --points-at $revision --list "v$version")
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read release tags.' }
    if ($matchingTags -contains "v$version") { $releaseTag = "v$version" }
}
[ordered]@{ version=$version; revision=$revision; branch=$branch; dirty=$dirty; channel=$channel;
    releaseTag=$releaseTag; backend='ela'; qt='6.10.2'; nativeComponents=$nativeComponents;
    assetAdapter=$assetAdapter;
    sourceStatusIncludesSubmodules=$false;
    preparationEvidenceSha256=(Get-FileHash -LiteralPath $PreparationEvidence -Algorithm SHA256).Hash.ToLowerInvariant();
    documentEditors=[ordered]@{ format='zeroslack.document-editors'; version=1; directory='resources/document-editors';
        marktext=$editorManifest.inputs.marktext.commit; blocksuite=$editorManifest.inputs.blocksuite.commit;
        webview2Sdk=$editorManifest.inputs.webview2.version; runtimeBundled=$false;
        packagedManifestSha256=(Get-FileHash -LiteralPath $editorManifestPath -Algorithm SHA256).Hash.ToLowerInvariant() };
    integratedFeatures=[ordered]@{ simdock=$integratedSimDock };
    appSuiteEnabled=[bool]$suiteBuild.enabled; distribution=$(if ($assemblySources) { $assemblySources.distribution } else { 'standalone' });
    suiteSdkVersion=$suiteBuild.sdkVersion; suiteGuiProvider=[bool]$suiteBuild.guiProvider;
    suiteCliClient=[bool]$suiteBuild.cliClient; runtimeBundled=$false;
    upstreamEla='454cac2d57a47d3cc28577dc817793aec1881ca7'; builtAtUtc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'build-info.json') -Encoding utf8
if ($Formal) {
    Invoke-ZeroSlackGate $python @('seal-package', '--package', $outputRoot, '--evidence', $PreparationEvidence)
}
Get-ChildItem -LiteralPath $outputRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/')
} | Set-Content -LiteralPath (Join-Path $outputRoot 'SHA256SUMS.txt') -Encoding utf8
Write-Output $outputRoot
