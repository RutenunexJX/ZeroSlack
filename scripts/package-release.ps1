[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/Desktop_Qt_6_10_2_MinGW_64_bit-Release',
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64'
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $sourceRoot $BuildDirectory }
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw 'Stage into a new directory before replacing a verified formal package.' }
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
if ($cache -notcontains 'ZEROSLACK_ENABLE_SUITEUI:BOOL=ON' -or
    $cache -notcontains 'ZEROSLACK_ENABLE_QLEMENTINE:BOOL=OFF') {
    throw 'The formal release requires SuiteUi ON and the direct preview backend OFF.'
}
$entry = $cache | Where-Object { $_ -match '^ZEROSLACK_SUITEUI_NOTICES_DIR:INTERNAL=(.+)$' } | Select-Object -First 1
if (-not $entry) { throw 'Missing SDK notice directory; reconfigure the build.' }
$sdkNotices = $entry.Substring($entry.IndexOf('=') + 1)
foreach ($name in @('NOTICE.txt', 'SuiteUi-Apache-2.0.txt', 'Qlementine-MIT.txt', 'Inter-OFL.txt',
                   'RobotoMono-Apache-2.0.txt', 'UPSTREAM.md', 'font-metadata.json', 'stop-all.patch', 'build-info.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdkNotices $name) -PathType Leaf)) { throw "Missing SDK notice: $name" }
}
foreach ($name in @('demo.exe', 'zeroslack-cli.exe', 'libzeroslack_core.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $name) -PathType Leaf)) { throw "Missing build output: $name" }
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'demo.exe') -Destination (Join-Path $outputRoot 'ZeroSlack.exe')
foreach ($name in @('zeroslack-cli.exe', 'libzeroslack_core.dll')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $outputRoot
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging/ZeroSlack-PACKAGE-README.txt') -Destination (Join-Path $outputRoot 'README.txt')
foreach ($name in @('用户手册.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $outputRoot
}
& (Join-Path $QtDirectory 'bin/windeployqt.exe') --release --compiler-runtime --no-translations --dir $outputRoot `
    (Join-Path $outputRoot 'ZeroSlack.exe') (Join-Path $outputRoot 'zeroslack-cli.exe') (Join-Path $outputRoot 'libzeroslack_core.dll')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
$licenseRoot = Join-Path $outputRoot 'licenses'
$sdkLicenseRoot = Join-Path $licenseRoot 'SuiteUi'
New-Item -ItemType Directory -Path $sdkLicenseRoot -Force | Out-Null
Get-ChildItem -LiteralPath $sdkNotices -File | Copy-Item -Destination $sdkLicenseRoot
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
foreach ($item in $licenses.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $item.Value) -Destination (Join-Path $licenseRoot $item.Key)
}
foreach ($name in @('COPYING3', 'COPYING3.LIB', 'COPYING.RUNTIME')) {
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory "licenses/gcc/$name") -Destination (Join-Path $licenseRoot "GCC-$name.txt")
}
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/gcc/COPYING3.LIB') -Destination (Join-Path $licenseRoot 'Qt-LGPLv3.txt')
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/winpthreads/COPYING') -Destination (Join-Path $licenseRoot 'winpthreads-COPYING.txt')
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/mingw-w64/COPYING.MinGW-w64.txt') -Destination (Join-Path $licenseRoot 'MinGW-w64-COPYING.txt')
Write-Output "Staged SuiteUi formal release: $outputRoot"
