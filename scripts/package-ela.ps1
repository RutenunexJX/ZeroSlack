[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/ela-migration',
    [string]$OutputDirectory = '',
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64',
    [switch]$Formal
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $sourceRoot 'VERSION') -Raw).Trim()
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $sourceRoot $BuildDirectory }
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
if ($cache -notcontains 'ZEROSLACK_ENABLE_ELA:BOOL=ON' -or
    $cache -notcontains 'ZEROSLACK_ENABLE_QLEMENTINE:BOOL=OFF' -or
    $cache -notcontains 'ZEROSLACK_ENABLE_SUITEUI:BOOL=OFF') { throw 'Expected an isolated Ela build.' }
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
if ($Formal -and $dirty) { throw 'Commit all source changes before creating a formal Ela package.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $sourceRoot 'build/packages/ZeroSlack-Ela-win64' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
# Stage only into a new directory; never overwrite the classic package or a running app.
if (Test-Path -LiteralPath $outputRoot) { throw 'Output must be a new directory.' }
$binaries = @('ZeroSlack-Ela.exe', 'zeroslack-cli.exe', 'libzeroslack_core.dll', 'ElaWidgetTools.dll')
foreach ($name in $binaries) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $name) -PathType Leaf)) { throw "Missing binary: $name" }
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
foreach ($name in $binaries) { Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $outputRoot }
& (Join-Path $QtDirectory 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-opengl-sw --dir $outputRoot `
    (Join-Path $outputRoot 'ZeroSlack-Ela.exe') (Join-Path $outputRoot 'libzeroslack_core.dll') `
    (Join-Path $outputRoot 'ElaWidgetTools.dll') (Join-Path $outputRoot 'zeroslack-cli.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
foreach ($name in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory "bin/$name") -Destination $outputRoot
}
$licenseRoot = Join-Path $outputRoot 'licenses'
New-Item -ItemType Directory -Path $licenseRoot | Out-Null
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
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging/ZeroSlack-Ela-README.txt') -Destination (Join-Path $outputRoot 'README.txt')
Set-Content -LiteralPath (Join-Path $outputRoot 'ela-package.txt') -Value 'ZeroSlack isolated Ela package' -Encoding ascii
$channel = if ($Formal) { 'formal' } else { 'preview' }
$releaseTag = if ($Formal) { "ela-v$version" } else { $null }
[ordered]@{ version=$version; revision=$revision; branch=$branch; dirty=$dirty; channel=$channel;
    releaseTag=$releaseTag; backend='ela'; qt='6.10.2';
    upstreamEla='454cac2d57a47d3cc28577dc817793aec1881ca7'; builtAtUtc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'build-info.json') -Encoding utf8
Get-ChildItem -LiteralPath $outputRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/')
} | Set-Content -LiteralPath (Join-Path $outputRoot 'SHA256SUMS.txt') -Encoding utf8
Write-Output $outputRoot
