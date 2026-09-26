[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/ela-migration',
    [string]$OutputDirectory = '',
    [string]$QtDirectory = 'E:/QT6/6.10.2/mingw_64',
    [string]$CompilerDirectory = 'E:/QT6/Tools/mingw1310_64',
    [switch]$Formal,
    [switch]$AllowDirty
)

# Compatibility entry point; Ela now ships as the sole ZeroSlack release.
& (Join-Path $PSScriptRoot 'package-release.ps1') @PSBoundParameters
