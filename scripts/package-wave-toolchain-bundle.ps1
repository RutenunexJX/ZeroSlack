[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Destination,

    [string]$ArchiveName = 'wave-toolchain-v1.zip',

    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$sourcePath = (Resolve-Path -LiteralPath $Source -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
    throw "Wave toolchain source is not a directory: $Source"
}

$requiredFiles = @(
    'toolchain-manifest.json',
    'verilator\bin\verilator.exe',
    'verilator\bin\verilator_bin.exe',
    'verilator\include\verilated.mk',
    'mingw\bin\g++.exe',
    'mingw\bin\mingw32-make.exe'
)
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourcePath $relative) `
            -PathType Leaf)) {
        throw "Wave toolchain source is incomplete: $relative"
    }
}
$reparsePoint = Get-ChildItem -LiteralPath $sourcePath -Recurse -Force |
    Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 } |
    Select-Object -First 1
if ($null -ne $reparsePoint) {
    throw "Wave toolchain source contains a link or reparse point: $($reparsePoint.FullName)"
}

$destinationPath = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
$archivePath = Join-Path $destinationPath $ArchiveName
$manifestPath = Join-Path $destinationPath 'wave-toolchain-bundle.json'

if ((Test-Path -LiteralPath $archivePath) -or
    (Test-Path -LiteralPath $manifestPath)) {
    if (-not $Force) {
        if (-not ((Test-Path -LiteralPath $archivePath -PathType Leaf) -and
                  (Test-Path -LiteralPath $manifestPath -PathType Leaf))) {
            throw 'The stable bundle is incomplete. Use -Force to replace it.'
        }
        $existing = Get-Content -LiteralPath $manifestPath -Raw |
            ConvertFrom-Json
        $actualHash = (Get-FileHash -LiteralPath $archivePath `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($existing.schema -ne 'zeroslack.wave-toolchain-bundle/v1' -or
            $existing.archive -ne $ArchiveName -or
            $existing.archiveSha256 -ne $actualHash) {
            throw 'The stable bundle does not match its manifest. Use -Force to replace it.'
        }
        Write-Host "Reusing stable Wave toolchain bundle: $archivePath"
        return
    }
    Remove-Item -LiteralPath $archivePath,$manifestPath `
        -Force -ErrorAction SilentlyContinue
}

$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
if (-not (Test-Path -LiteralPath $tar -PathType Leaf)) {
    $tarCommand = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($null -eq $tarCommand) {
        throw 'Windows tar.exe is required to create the Wave toolchain bundle.'
    }
    $tar = $tarCommand.Source
}

$stagingRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("zeroslack-wave-bundle-" + [Guid]::NewGuid().ToString('N'))
$stagingArchive = Join-Path $stagingRoot $ArchiveName
$stagingManifest = Join-Path $stagingRoot 'wave-toolchain-bundle.json'
try {
    New-Item -ItemType Directory -Path $stagingRoot | Out-Null
    & $tar -a -cf $stagingArchive -C $sourcePath .
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $stagingArchive -PathType Leaf)) {
        throw 'Failed to create the Wave toolchain archive.'
    }

    $hash = (Get-FileHash -LiteralPath $stagingArchive `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    $archiveBytes = (Get-Item -LiteralPath $stagingArchive).Length
    $manifest = [ordered]@{
        schema = 'zeroslack.wave-toolchain-bundle/v1'
        schemaVersion = 1
        bundleId = "sha256-$hash"
        archive = $ArchiveName
        archiveSha256 = $hash
        archiveBytes = $archiveBytes
    }
    $manifest | ConvertTo-Json -Depth 3 | Set-Content `
        -LiteralPath $stagingManifest -Encoding utf8NoBOM

    Move-Item -LiteralPath $stagingArchive -Destination $archivePath
    Move-Item -LiteralPath $stagingManifest -Destination $manifestPath
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}

Write-Host ('Wave toolchain bundle: {0:N1} MiB at {1}' -f `
    ((Get-Item -LiteralPath $archivePath).Length / 1MB), $archivePath)
