[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/Desktop_Qt_6_10_2_MinGW_64_bit-Release',
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64',
    [string]$OutputDirectory = '',
    [switch]$UpdatePreview
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $sourceRoot 'VERSION') -Raw).Trim()
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $sourceRoot $BuildDirectory }
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
$usesSdk = [bool]($cache -match '^ZEROSLACK_ENABLE_SUITEUI:BOOL=ON$')
$sdkNotices = ''
if ($usesSdk) {
    $noticeEntry = $cache | Where-Object { $_ -match '^ZEROSLACK_SUITEUI_NOTICES_DIR:INTERNAL=(.+)$' } | Select-Object -First 1
    if (-not $noticeEntry) { throw 'Missing SDK notice directory; reconfigure the SDK build before packaging.' }
    $sdkNotices = $noticeEntry.Substring($noticeEntry.IndexOf('=') + 1)
    foreach ($name in @('NOTICE.txt', 'SuiteUi-Apache-2.0.txt', 'Qlementine-MIT.txt', 'Inter-OFL.txt',
                       'RobotoMono-Apache-2.0.txt', 'UPSTREAM.md', 'font-metadata.json', 'stop-all.patch', 'build-info.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdkNotices $name))) { throw "Missing SDK notice: $name" }
    }
} elseif (-not ($cache -match '^ZEROSLACK_ENABLE_QLEMENTINE:BOOL=ON$')) {
    throw 'This build does not enable an optional UI preview backend.'
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $sourceRoot "build/previews/ZeroSlack-Qlementine-$version" }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$marker = Join-Path $outputRoot 'qlementine-preview.txt'
if (Test-Path -LiteralPath $outputRoot) {
    if (-not $UpdatePreview -or -not (Test-Path -LiteralPath $marker) -or
        (Get-Content -LiteralPath $marker -Raw).Trim() -ne 'ZeroSlack isolated Qlementine preview') {
        throw 'Refusing to overwrite a directory not marked as this independent preview.'
    }
}
foreach ($name in @('ZeroSlack-Qlementine-Preview.exe', 'libzeroslack_core.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot $name))) { throw "Missing build output: $name" }
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Set-Content -LiteralPath $marker -Value 'ZeroSlack isolated Qlementine preview' -Encoding ascii
foreach ($name in @('ZeroSlack-Qlementine-Preview.exe', 'libzeroslack_core.dll')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination (Join-Path $outputRoot $name)
}
& (Join-Path $QtDirectory 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-opengl-sw --dir $outputRoot `
    (Join-Path $outputRoot 'ZeroSlack-Qlementine-Preview.exe') (Join-Path $outputRoot 'libzeroslack_core.dll')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed: $LASTEXITCODE" }
foreach ($name in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory "bin/$name") -Destination $outputRoot
}
$licenseRoot = Join-Path $outputRoot 'licenses'
New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
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
if (-not $usesSdk) {
    $licenses['Qlementine-MIT.txt'] = 'thirdparty/qlementine/LICENSE'
    $licenses['Qlementine-UPSTREAM.md'] = 'thirdparty/qlementine/UPSTREAM.md'
    $licenses['Qlementine-font-metadata.json'] = 'thirdparty/qlementine/font-metadata.json'
    $licenses['Qlementine-stop-all.patch'] = 'thirdparty/qlementine/patches/stop-all.patch'
    $licenses['Inter-OFL.txt'] = 'thirdparty/qlementine/licenses/Inter-OFL.txt'
    $licenses['RobotoMono-Apache-2.0.txt'] = 'thirdparty/qlementine/licenses/RobotoMono-Apache-2.0.txt'
}
foreach ($entry in $licenses.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $entry.Value) -Destination (Join-Path $licenseRoot $entry.Key)
}
if ($usesSdk) {
    $sdkLicenseRoot = Join-Path $licenseRoot 'SuiteUi'
    New-Item -ItemType Directory -Force -Path $sdkLicenseRoot | Out-Null
    Get-ChildItem -LiteralPath $sdkNotices -File | Copy-Item -Destination $sdkLicenseRoot
}
foreach ($name in @('COPYING3', 'COPYING3.LIB', 'COPYING.RUNTIME')) {
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory "licenses/gcc/$name") -Destination (Join-Path $licenseRoot "GCC-$name.txt")
}
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/gcc/COPYING3.LIB') -Destination (Join-Path $licenseRoot 'Qt-LGPLv3.txt')
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/winpthreads/COPYING') -Destination (Join-Path $licenseRoot 'winpthreads-COPYING.txt')
Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'licenses/mingw-w64/COPYING.MinGW-w64.txt') -Destination (Join-Path $licenseRoot 'MinGW-w64-COPYING.txt')
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $outputRoot
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging/Qlementine-PREVIEW-README.md') -Destination (Join-Path $outputRoot 'README.md')
$manifest = Get-ChildItem -LiteralPath $outputRoot -File -Recurse | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/')
}
$manifest | Set-Content -LiteralPath (Join-Path $outputRoot 'SHA256SUMS.txt') -Encoding ascii
Write-Output $outputRoot
