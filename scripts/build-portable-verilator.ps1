[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [Parameter(Mandatory = $true)]
    [string]$InstallRoot,

    [Parameter(Mandatory = $true)]
    [string]$WinFlexBisonRoot,

    [string]$MinGWRoot = 'E:\QT6\Tools\mingw1310_64',
    [string]$CMakeProgram = 'E:\QT6\Tools\CMake_64\bin\cmake.exe',
    [string]$NinjaProgram = 'E:\QT6\Tools\Ninja\ninja.exe',
    [int]$Parallel = 4
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-ExistingDirectory([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Container)) {
        throw "$Label is not a directory: $Path"
    }
    return $resolved.Path
}

function Resolve-ExistingFile([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "$Label is not a file: $Path"
    }
    return $resolved.Path
}

$source = Resolve-ExistingDirectory $SourceRoot 'Verilator source root'
$winFlexBison = Resolve-ExistingDirectory $WinFlexBisonRoot 'WinFlexBison root'
$mingw = Resolve-ExistingDirectory $MinGWRoot 'MinGW root'
$cmake = Resolve-ExistingFile $CMakeProgram 'CMake executable'
$ninja = Resolve-ExistingFile $NinjaProgram 'Ninja executable'
$gxx = Resolve-ExistingFile (Join-Path $mingw 'bin\g++.exe') 'MinGW g++'
$patch = Resolve-ExistingFile `
    (Join-Path $repoRoot 'packaging\verilator-v5.050-windows-portable.patch') `
    'Verilator portable patch'

& git -C $source apply --unidiff-zero --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    & git -C $source apply --unidiff-zero --check $patch
    if ($LASTEXITCODE -ne 0) {
        throw 'Verilator source does not match the pinned v5.050 portable patch.'
    }
    & git -C $source apply --unidiff-zero $patch
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to apply the portable Verilator patch.'
    }
}

$build = [System.IO.Path]::GetFullPath($BuildRoot)
$install = [System.IO.Path]::GetFullPath($InstallRoot)
$previousPath = $env:PATH
$previousWinFlexBison = $env:WIN_FLEX_BISON
try {
    $env:PATH = "$(Join-Path $mingw 'bin');$previousPath"
    $env:WIN_FLEX_BISON = $winFlexBison
    & $cmake -S $source -B $build -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_CXX_COMPILER=$gxx" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_INSTALL_PREFIX=$install"
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable Verilator configure failed.'
    }
    & $cmake --build $build --target install --parallel $Parallel
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable Verilator build failed.'
    }
}
finally {
    $env:PATH = $previousPath
    if ($null -eq $previousWinFlexBison) {
        Remove-Item Env:WIN_FLEX_BISON -ErrorAction SilentlyContinue
    }
    else {
        $env:WIN_FLEX_BISON = $previousWinFlexBison
    }
}

Write-Host "Portable Verilator installed at $install"
