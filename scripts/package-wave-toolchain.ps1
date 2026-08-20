[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Destination,

    [Parameter(Mandatory = $true)]
    [string]$VerilatorInstallRoot,

    [Parameter(Mandatory = $true)]
    [string]$VerilatorSourceRoot,

    [string]$MinGWRoot = 'E:\QT6\Tools\mingw1310_64'
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

function Replace-RequiredRegex(
    [string]$Text,
    [string]$Pattern,
    [string]$Replacement,
    [string]$Label
) {
    if (-not [regex]::IsMatch($Text, $Pattern)) {
        throw "Portable Verilator patch mismatch: $Label"
    }
    return [regex]::Replace($Text, $Pattern, $Replacement)
}

$verilatorSource = Resolve-ExistingDirectory $VerilatorSourceRoot 'Verilator source root'
$verilatorInstall = Resolve-ExistingDirectory $VerilatorInstallRoot 'Verilator install root'
$mingw = Resolve-ExistingDirectory $MinGWRoot 'MinGW root'
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$destinationParent = Split-Path -Parent $destinationPath
if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
    New-Item -ItemType Directory -Path $destinationParent | Out-Null
}
if (Test-Path -LiteralPath $destinationPath) {
    throw "Toolchain destination already exists: $destinationPath"
}

$staging = "$destinationPath.__staging_$([Guid]::NewGuid().ToString('N'))"
try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    $verilatorDestination = Join-Path $staging 'verilator'
    $mingwDestination = Join-Path $staging 'mingw'
    Copy-Item -LiteralPath $verilatorInstall -Destination $verilatorDestination -Recurse
    Copy-Item -LiteralPath $mingw -Destination $mingwDestination -Recurse

    $gxx = Join-Path $mingwDestination 'bin\g++.exe'
    $includerSource = Join-Path $repoRoot 'packaging\verilator_includer.cpp'
    $includer = Join-Path $verilatorDestination 'bin\verilator_includer.exe'
    $launcherSource = Join-Path $repoRoot 'packaging\verilator_launcher.cpp'
    $launcher = Join-Path $verilatorDestination 'bin\verilator.exe'
    $verilatorBinary = Join-Path $verilatorDestination 'bin\verilator_bin.exe'
    $previousPath = $env:PATH
    try {
        $env:PATH = "$(Join-Path $mingwDestination 'bin');$previousPath"
        & $gxx -std=c++20 -Os -static -static-libgcc -static-libstdc++ `
            $includerSource -o $includer
        $includerExitCode = $LASTEXITCODE
        $rawVerilatorVersion = (& $verilatorBinary --version 2>&1 |
            Out-String).Trim()
        if ($rawVerilatorVersion -notmatch '(?i)\bv?(\d+\.\d+(?:\.\d+)?)') {
            throw "Cannot parse Verilator version: $rawVerilatorVersion"
        }
        $verilatorVersionNumber = $Matches[1]
        $verilatorVersion = "Verilator $verilatorVersionNumber"
        $verilatorVersionNumber | Set-Content `
            -LiteralPath (Join-Path $verilatorDestination `
                'bin\verilator-version.txt') -Encoding ascii -NoNewline
        & $gxx -std=c++20 -Os -municode -static -static-libgcc `
            -static-libstdc++ $launcherSource -o $launcher
        $launcherExitCode = $LASTEXITCODE
    }
    finally {
        $env:PATH = $previousPath
    }
    if ($includerExitCode -ne 0 -or
        -not (Test-Path -LiteralPath $includer)) {
        throw 'Failed to build the portable Verilator includer.'
    }
    if ($launcherExitCode -ne 0 -or
        -not (Test-Path -LiteralPath $launcher)) {
        throw 'Failed to build the portable Verilator launcher.'
    }

    $verilatedMake = Join-Path $verilatorDestination 'include\verilated.mk'
    $makeText = [System.IO.File]::ReadAllText($verilatedMake)
    $makeText = Replace-RequiredRegex $makeText '(?m)^AR\s*=.*$' `
        'AR = ar' 'AR setting'
    $makeText = Replace-RequiredRegex $makeText '(?m)^CXX\s*=.*$' `
        'CXX = g++' 'CXX setting'
    $makeText = Replace-RequiredRegex $makeText '(?m)^LINK\s*=.*$' `
        'LINK = g++' 'LINK setting'
    $makeText = Replace-RequiredRegex $makeText '(?m)^PERL\s*=.*$' `
        'PERL = perl' 'PERL setting'
    $makeText = Replace-RequiredRegex $makeText '(?m)^PYTHON3\s*=.*$' `
        'PYTHON3 = python3' 'PYTHON3 setting'
    $makeText = Replace-RequiredRegex `
        $makeText `
        '(?m)^VERILATOR_INCLUDER\s*=.*$' `
        'VERILATOR_INCLUDER = "$(VERILATOR_ROOT)/bin/verilator_includer.exe"' `
        'native includer setting'
    foreach ($requiredFragment in @(
        'ifeq ($(OS),Windows_NT)',
        '$(VERILATOR_ROOT_MAKE)/include/%.cpp'
    )) {
        if (-not $makeText.Contains($requiredFragment)) {
            throw "Verilator install is missing the portable Windows patch: $requiredFragment"
        }
    }
    [System.IO.File]::WriteAllText(
        $verilatedMake,
        $makeText,
        [System.Text.UTF8Encoding]::new($false))

    $smokeRoot = Join-Path $staging '_portable_smoke'
    New-Item -ItemType Directory -Path $smokeRoot | Out-Null
    @'
module top;
    initial begin
        $display("zeroslack-portable-toolchain-ok");
        $finish;
    end
endmodule
'@ | Set-Content -LiteralPath (Join-Path $smokeRoot 'top.sv') `
        -Encoding utf8NoBOM
    @'
#include "Vtop.h"
#include "verilated.h"

#include <memory>

int main(int argc, char** argv)
{
    auto context = std::make_unique<VerilatedContext>();
    context->commandArgs(argc, argv);
    auto top = std::make_unique<Vtop>(context.get());
    top->eval();
    top->final();
    return 0;
}
'@ | Set-Content -LiteralPath (Join-Path $smokeRoot 'main.cpp') `
        -Encoding utf8NoBOM
    $previousPath = $env:PATH
    $previousVerilatorRoot = $env:VERILATOR_ROOT
    $previousMake = $env:MAKE
    try {
        $env:PATH = "$(Join-Path $mingwDestination 'bin');$env:SystemRoot\System32"
        $env:VERILATOR_ROOT = $verilatorDestination.Replace('\', '/')
        $env:MAKE = 'mingw32-make.exe'
        Push-Location $smokeRoot
        try {
            $objectDirectory = Join-Path $smokeRoot 'obj_dir'
            $buildOutput = (& $launcher --cc --exe --build `
                --top-module top --prefix Vtop --Mdir $objectDirectory `
                -o toolchain-smoke.exe top.sv main.cpp 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0) {
                throw "Portable Verilator compile smoke failed:`n$buildOutput"
            }
            $runOutput = (& (Join-Path $objectDirectory 'toolchain-smoke.exe') `
                2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0 -or
                -not $runOutput.Contains('zeroslack-portable-toolchain-ok')) {
                throw "Portable Verilator run smoke failed:`n$runOutput"
            }
        }
        finally {
            Pop-Location
        }
    }
    finally {
        $env:PATH = $previousPath
        if ($null -eq $previousVerilatorRoot) {
            Remove-Item Env:VERILATOR_ROOT -ErrorAction SilentlyContinue
        }
        else {
            $env:VERILATOR_ROOT = $previousVerilatorRoot
        }
        if ($null -eq $previousMake) {
            Remove-Item Env:MAKE -ErrorAction SilentlyContinue
        }
        else {
            $env:MAKE = $previousMake
        }
    }
    Remove-Item -LiteralPath $smokeRoot -Recurse -Force

    $licenseRoot = Join-Path $staging 'licenses'
    New-Item -ItemType Directory -Path $licenseRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $verilatorSource 'LICENSE') `
        -Destination (Join-Path $licenseRoot 'VERILATOR-LICENSE.txt')
    if (Test-Path -LiteralPath (Join-Path $mingwDestination 'licenses')) {
        Copy-Item -LiteralPath (Join-Path $mingwDestination 'licenses') `
            -Destination (Join-Path $licenseRoot 'mingw') -Recurse
    }

    $compilerVersion = (& $gxx --version | Select-Object -First 1).Trim()
    $manifest = [ordered]@{
        schema = 'zeroslack.wave-toolchain/v1'
        architecture = 'windows-x86_64'
        verilator = $verilatorVersion
        compiler = $compilerVersion
        make = 'mingw32-make.exe'
        portablePatch = 'verilator-v5.050-windows-portable/v1'
        layout = [ordered]@{
            verilator = 'verilator'
            compiler = 'mingw'
        }
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content `
        -LiteralPath (Join-Path $staging 'toolchain-manifest.json') `
        -Encoding utf8NoBOM

    @"
ZeroSlack portable Wave Simulation toolchain

This directory is self-contained. ZeroSlack discovers Verilator, GNU Make,
and MinGW automatically; no system PATH changes are required.

$verilatorVersion
$compilerVersion
"@ | Set-Content -LiteralPath (Join-Path $staging 'README.txt') `
        -Encoding utf8NoBOM

    Move-Item -LiteralPath $staging -Destination $destinationPath
}
catch {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
    throw
}

$bytes = (Get-ChildItem -LiteralPath $destinationPath -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
Write-Host ('Portable Wave toolchain: {0:N1} MiB at {1}' -f ($bytes / 1MB), $destinationPath)
