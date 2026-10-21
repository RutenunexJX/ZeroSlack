[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/ela-migration',
    [string]$OutputDirectory = '',
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64',
    [string]$XipsPackageDirectory = '',
    [string]$XipsAssetAdapterDirectory = '',
    [switch]$Formal,
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $sourceRoot 'VERSION') -Raw).Trim()
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $sourceRoot $BuildDirectory }
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
if ($cache -notcontains 'ZEROSLACK_ENABLE_ELA:BOOL=ON' -or
    $cache -notcontains 'ZEROSLACK_ENABLE_QLEMENTINE:BOOL=OFF' -or
    $cache -notcontains 'ZEROSLACK_ENABLE_SUITEUI:BOOL=OFF') { throw 'The maintained release requires the Ela backend.' }
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
$revision = & git -C $sourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git revision.' }
$branch = & git -C $sourceRoot branch --show-current
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git branch.' }
$dirty = [bool](& git -C $sourceRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git source status.' }
if ($Formal -and $dirty -and -not $AllowDirty) {
    throw 'Formal packages require clean source unless -AllowDirty is explicitly requested.'
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $sourceRoot 'build/packages/ZeroSlack-win64' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
# Verify the new staging directory before replacing the installed package.
if (Test-Path -LiteralPath $outputRoot) { throw 'Output must be a new directory.' }
$binaries = @('ZeroSlack.exe', 'zeroslack-cli.exe', 'libzeroslack_core.dll',
    'libzeroslack_semantic.dll', 'libzeroslack_documents.dll', 'ElaWidgetTools.dll')
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
        $adapterInfo.coreVersion -ne $nativeComponents.xips.version -or
        $adapterInfo.coreCommit -ne $nativeComponents.xips.revision) {
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
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.md', '用户手册.md')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $outputRoot
}
# Ship editable user-template definitions and their non-destructive merge tool.
Copy-Item -LiteralPath (Join-Path $sourceRoot 'resources/templates') -Destination $outputRoot -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging/ZeroSlack-PACKAGE-README.txt') -Destination (Join-Path $outputRoot 'README.txt')
# Public CLI documentation and the headless Qt platform are runtime payload.
New-Item -ItemType Directory -Path (Join-Path $outputRoot 'docs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs/cli-automation.md') -Destination (Join-Path $outputRoot 'docs/cli-automation.md')
Copy-Item -LiteralPath (Join-Path $QtDirectory 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $outputRoot 'platforms/qoffscreen.dll')
$channel = if ($Formal) { 'formal' } else { 'preview' }
$releaseTag = $null
if ($Formal -and -not $dirty) {
    $matchingTags = @(& git -C $sourceRoot tag --points-at $revision --list "v$version")
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read release tags.' }
    if ($matchingTags -contains "v$version") { $releaseTag = "v$version" }
}
[ordered]@{ version=$version; revision=$revision; branch=$branch; dirty=$dirty; channel=$channel;
    releaseTag=$releaseTag; backend='ela'; qt='6.10.2'; nativeComponents=$nativeComponents;
    assetAdapter=$assetAdapter;
    integratedFeatures=[ordered]@{ simdock=$integratedSimDock };
    appSuiteEnabled=[bool]$suiteBuild.enabled; distribution='standalone';
    suiteSdkVersion=$suiteBuild.sdkVersion; suiteGuiProvider=[bool]$suiteBuild.guiProvider;
    suiteCliClient=[bool]$suiteBuild.cliClient; runtimeBundled=$false;
    upstreamEla='454cac2d57a47d3cc28577dc817793aec1881ca7'; builtAtUtc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'build-info.json') -Encoding utf8
Get-ChildItem -LiteralPath $outputRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/')
} | Set-Content -LiteralPath (Join-Path $outputRoot 'SHA256SUMS.txt') -Encoding utf8
Write-Output $outputRoot
