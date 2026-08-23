[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ZeroSlackDirectory,

    [Parameter(Mandatory = $true)]
    [string]$PinloomDirectory,

    [Parameter(Mandatory = $true)]
    [string]$WaveWorkbenchDirectory,

    [Parameter(Mandatory = $true)]
    [string]$RegMapWorkbenchDirectory,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeInstallDirectory,

    [Parameter(Mandatory = $true)]
    [string]$ToolchainDirectory,

    [string]$QtBinDirectory = "E:\QT6\6.10.2\mingw_64\bin",
    [string]$OutputRoot = "E:\PinloomRoot\AppPackage",
    [string]$SuiteName = "AppSuite",
    [string]$ZeroSlackVersion = "",
    [string]$PinloomVersion = "",
    [string]$WaveWorkbenchVersion = "",
    [string]$RegMapWorkbenchVersion = "",
    [string]$RuntimeVersion = "",
    [switch]$ReplaceExisting
)

$ErrorActionPreference = "Stop"

function Resolve-RequiredDirectory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label directory does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-RequiredFile([string]$Root, [string]$RelativePath) {
    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package file is missing: $path"
    }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Get-ChildItem -Force -LiteralPath $Source | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Read-ExecutableVersion([string]$Executable) {
    $version = (Get-Item -LiteralPath $Executable).VersionInfo.ProductVersion
    if ([string]::IsNullOrWhiteSpace($version)) {
        return "unknown"
    }
    return $version.Trim()
}

function Resolve-ComponentVersion([string]$ExplicitVersion, [string]$Executable) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitVersion)) {
        return $ExplicitVersion.Trim()
    }
    return Read-ExecutableVersion $Executable
}

$zeroSlackSource = Resolve-RequiredDirectory $ZeroSlackDirectory "ZeroSlack"
$pinloomSource = Resolve-RequiredDirectory $PinloomDirectory "Pinloom"
$waveSource = Resolve-RequiredDirectory $WaveWorkbenchDirectory "WaveWorkbench"
$regMapSource = Resolve-RequiredDirectory $RegMapWorkbenchDirectory "RegMapWorkbench"
$runtimeInstall = Resolve-RequiredDirectory $RuntimeInstallDirectory "Suite Runtime install"
$toolchainSource = Resolve-RequiredDirectory $ToolchainDirectory "Wave toolchain"
$qtBin = Resolve-RequiredDirectory $QtBinDirectory "Qt bin"

Assert-RequiredFile $zeroSlackSource "ZeroSlack.exe"
Assert-RequiredFile $pinloomSource "pinloom_app.exe"
Assert-RequiredFile $waveSource "wave-workbench.exe"
Assert-RequiredFile $waveSource "wavewidgets.dll"
Assert-RequiredFile $regMapSource "RegMapWorkbench.exe"
Assert-RequiredFile $regMapSource "regmapc.exe"
Assert-RequiredFile $runtimeInstall "bin\suite-runtime.exe"
Assert-RequiredFile $runtimeInstall "bin\suite-cli.exe"
Assert-RequiredFile $toolchainSource "wave-toolchain-bundle.json"
Assert-RequiredFile $toolchainSource "wave-toolchain-v1.zip"
Assert-RequiredFile $qtBin "windeployqt.exe"

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$outputRootPath = (Resolve-Path -LiteralPath $OutputRoot).Path
$suiteDirectory = Join-Path $outputRootPath $SuiteName
$stagingDirectory = Join-Path $outputRootPath (".{0}.staging-{1}" -f $SuiteName, $PID)

$expectedParent = [System.IO.Path]::GetFullPath($outputRootPath).TrimEnd('\') + '\'
foreach ($candidate in @($suiteDirectory, $stagingDirectory)) {
    $fullCandidate = [System.IO.Path]::GetFullPath($candidate)
    if (-not $fullCandidate.StartsWith(
            $expectedParent,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package path is outside OutputRoot: $fullCandidate"
    }
}
if ((Test-Path -LiteralPath $suiteDirectory) -and -not $ReplaceExisting) {
    throw "Suite package already exists. Pass -ReplaceExisting to rebuild it."
}
if (Test-Path -LiteralPath $stagingDirectory) {
    Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
}

$appsDirectory = Join-Path $stagingDirectory "Apps"
$zeroSlackTarget = Join-Path $appsDirectory "ZeroSlack-win64"
$pinloomTarget = Join-Path $appsDirectory "Pinloom"
$waveTarget = Join-Path $appsDirectory "WaveWorkbench"
$regMapTarget = Join-Path $appsDirectory "RegMapWorkbench"
$runtimeTarget = Join-Path $appsDirectory "Runtime"
$toolchainTarget = Join-Path $appsDirectory "Toolchain"

Copy-DirectoryContents $zeroSlackSource $zeroSlackTarget
$duplicateWave = Join-Path $zeroSlackTarget "WaveWorkbench"
if (Test-Path -LiteralPath $duplicateWave -PathType Container) {
    Remove-Item -LiteralPath $duplicateWave -Recurse -Force
}
Copy-DirectoryContents $pinloomSource $pinloomTarget
Copy-DirectoryContents $waveSource $waveTarget
Copy-DirectoryContents $regMapSource $regMapTarget
Copy-DirectoryContents $toolchainSource $toolchainTarget

New-Item -ItemType Directory -Path $runtimeTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $runtimeInstall "bin\suite-runtime.exe") `
    -Destination $runtimeTarget
Copy-Item -LiteralPath (Join-Path $runtimeInstall "bin\suite-cli.exe") `
    -Destination $runtimeTarget
$installedSchemas = Join-Path $runtimeInstall "share\suiteapp\schemas"
if (Test-Path -LiteralPath $installedSchemas -PathType Container) {
    Copy-DirectoryContents $installedSchemas (Join-Path $runtimeTarget "schemas")
}

$deployTool = Join-Path $qtBin "windeployqt.exe"
foreach ($runtimeExecutable in @("suite-runtime.exe", "suite-cli.exe")) {
    & $deployTool --release --compiler-runtime --no-translations `
        --dir $runtimeTarget (Join-Path $runtimeTarget $runtimeExecutable)
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed for $runtimeExecutable with exit code $LASTEXITCODE"
    }
}

$components = @(
    [ordered]@{
        id = "zeroslack"
        version = Resolve-ComponentVersion `
            $ZeroSlackVersion (Join-Path $zeroSlackTarget "ZeroSlack.exe")
        executable = "Apps/ZeroSlack-win64/ZeroSlack.exe"
    },
    [ordered]@{
        id = "pinloom"
        version = Resolve-ComponentVersion `
            $PinloomVersion (Join-Path $pinloomTarget "pinloom_app.exe")
        executable = "Apps/Pinloom/pinloom_app.exe"
    },
    [ordered]@{
        id = "wave"
        version = Resolve-ComponentVersion `
            $WaveWorkbenchVersion (Join-Path $waveTarget "wave-workbench.exe")
        executable = "Apps/WaveWorkbench/wave-workbench.exe"
        nativeSurfaceAbi = 1
    },
    [ordered]@{
        id = "regmap"
        version = Resolve-ComponentVersion `
            $RegMapWorkbenchVersion (Join-Path $regMapTarget "RegMapWorkbench.exe")
        executable = "Apps/RegMapWorkbench/RegMapWorkbench.exe"
    }
)
$manifest = [ordered]@{
    schema = "suite-package/v1"
    protocol = "suite-app/v1"
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    runtime = [ordered]@{
        version = Resolve-ComponentVersion `
            $RuntimeVersion (Join-Path $runtimeTarget "suite-runtime.exe")
        executable = "Apps/Runtime/suite-runtime.exe"
    }
    components = $components
}
$manifestPath = Join-Path $stagingDirectory "suite-manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$hashLines = Get-ChildItem -Recurse -File -LiteralPath $stagingDirectory |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = [System.IO.Path]::GetRelativePath(
            $stagingDirectory, $_.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
$hashLines | Set-Content -LiteralPath `
    (Join-Path $stagingDirectory "SHA256SUMS.txt") -Encoding utf8

if (Test-Path -LiteralPath $suiteDirectory) {
    Remove-Item -LiteralPath $suiteDirectory -Recurse -Force
}
Move-Item -LiteralPath $stagingDirectory -Destination $suiteDirectory

$files = Get-ChildItem -Recurse -File -LiteralPath $suiteDirectory
[PSCustomObject]@{
    SuiteDirectory = $suiteDirectory
    FileCount = $files.Count
    PackageBytes = ($files | Measure-Object Length -Sum).Sum
    Manifest = Join-Path $suiteDirectory "suite-manifest.json"
}
