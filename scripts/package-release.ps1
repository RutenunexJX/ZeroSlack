[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/ela-migration',
    [string]$OutputDirectory = '',
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64',
    [string]$XipsPackageDirectory = '',
    [string]$SimDockPackageDirectory = '',
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
$binaries = @('ZeroSlack.exe', 'zeroslack-cli.exe', 'libzeroslack_core.dll', 'ElaWidgetTools.dll')
foreach ($name in $binaries) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $name) -PathType Leaf)) { throw "Missing binary: $name" }
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
foreach ($name in $binaries) { Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $outputRoot }
$deployTargets = @($binaries | ForEach-Object { Join-Path $outputRoot $_ })
$nativeComponents = [ordered]@{}
foreach ($component in @(
    @{ id='xips'; package=$XipsPackageDirectory; setting='ZEROSLACK_XIPS_COMPONENT_DIR'; files=@('xips-browser.dll','xips-browser-impl.dll','XipsEla.dll') },
    @{ id='simdock'; package=$SimDockPackageDirectory; setting='ZEROSLACK_SIMDOCK_COMPONENT_DIR'; files=@('simdock-workbench.dll','SimDockEla.dll','simdock-workbench.json') }
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
    if ($component.id -eq 'simdock') {
        $workbench = Get-Content -LiteralPath (Join-Path $componentRoot 'simdock-workbench.json') -Raw | ConvertFrom-Json
        $hasWave = Test-Path -LiteralPath (Join-Path $componentRoot 'wavewidgets.dll')
        if ($hasWave -or (Test-Path -LiteralPath (Join-Path $componentRoot 'WaveWorkbenchEla.dll'))) {
            $componentFiles += @('wavewidgets.dll', 'WaveWorkbenchEla.dll')
        }
    }
    $componentTarget = Join-Path $outputRoot "components/$($component.id)"
    New-Item -ItemType Directory -Path $componentTarget | Out-Null
    foreach ($name in $componentFiles) {
        $source = Join-Path $componentRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing native runtime: $source" }
        $target = Join-Path $componentTarget $name
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
            Copy-Item -LiteralPath $path -Destination $componentNotices -Recurse
        }
    }
    $nativeComponents[$component.id] = [ordered]@{
        directory="components/$($component.id)"; files=$componentFiles
        version=$(if ($componentInfo) { $componentInfo.version } else { $null })
        revision=$(if ($componentInfo.revision) { $componentInfo.revision } elseif ($componentInfo.sourceCommit) { $componentInfo.sourceCommit } else { $null })
    }
}
& (Join-Path $QtDirectory 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-opengl-sw --dir $outputRoot @deployTargets
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
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
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging/ZeroSlack-PACKAGE-README.txt') -Destination (Join-Path $outputRoot 'README.txt')
$channel = if ($Formal) { 'formal' } else { 'preview' }
$releaseTag = $null
if ($Formal -and -not $dirty) {
    $matchingTags = @(& git -C $sourceRoot tag --points-at $revision --list "v$version")
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read release tags.' }
    if ($matchingTags -contains "v$version") { $releaseTag = "v$version" }
}
[ordered]@{ version=$version; revision=$revision; branch=$branch; dirty=$dirty; channel=$channel;
    releaseTag=$releaseTag; backend='ela'; qt='6.10.2'; nativeComponents=$nativeComponents;
    appSuiteEnabled=$false; distribution='standalone';
    upstreamEla='454cac2d57a47d3cc28577dc817793aec1881ca7'; builtAtUtc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'build-info.json') -Encoding utf8
Get-ChildItem -LiteralPath $outputRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/')
} | Set-Content -LiteralPath (Join-Path $outputRoot 'SHA256SUMS.txt') -Encoding utf8
Write-Output $outputRoot
