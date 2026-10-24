[CmdletBinding()]
param([string]$Directory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Directory).Path.TrimEnd('\','/')
function ResourcePath([string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        $Relative.Contains(':') -or $Relative.Contains('\') -or $Relative -match '(^|/)\.\.(/|$)') {
        throw "Invalid resource path: $Relative"
    }
    $path = [IO.Path]::GetFullPath((Join-Path $root $Relative))
    if (-not $path.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Resource escapes bundle: $Relative"
    }
    for ($walk = $path; $walk -ne $root; $walk = Split-Path -Parent $walk) {
        if ((Get-Item -LiteralPath $walk -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Linked resource paths are not supported by this helper: $walk"
        }
    }
    return $path
}
function AtomicText([string]$Path, [string]$Text) {
    $temporary = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary, $Text, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}
$manifestPath = ResourcePath 'manifest.json'
if ((Get-Item -LiteralPath $manifestPath).Length -gt 65536) { throw 'Manifest exceeds 64 KiB.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($manifest.format -ne 'zeroslack.welcome.bundle' -or $manifest.version -ne 1) { throw 'Unsupported bundle format/version.' }
foreach ($id in @('rtl','waveform','registers')) {
    $entry = $manifest.cards.$id
    $path = ResourcePath $entry.scene
    if ((Get-Item -LiteralPath $path).Length -gt 262144) { throw "Scene exceeds 256 KiB: $path" }
    $scene = Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json
    if ($scene.format -ne 'zeroslack.welcome.scene' -or $scene.version -ne 1) { throw "Unsupported scene format/version: $path" }
    $entry.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
Copy-Item -LiteralPath $manifestPath -Destination ($manifestPath + '.previous') -Force
AtomicText $manifestPath (($manifest | ConvertTo-Json -Depth 12) + "`n")

# The package's binary identities remain unchanged. Refresh only resource entries.
$resources = Split-Path -Parent $root
$package = Split-Path -Parent $resources
$sums = Join-Path $package 'SHA256SUMS.txt'
if ((Split-Path -Leaf $root) -eq 'welcome' -and (Split-Path -Leaf $resources) -eq 'resources' -and
    (Test-Path -LiteralPath $sums -PathType Leaf)) {
    $lines = @(Get-Content -LiteralPath $sums -Encoding utf8 | Where-Object { $_ -notmatch '^[0-9a-fA-F]{64}\s+resources/welcome/' })
    $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($root)
    $files = [Collections.Generic.List[string]]::new()
    while ($pending.Count -gt 0) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked resource entry: $($item.FullName)" }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) } else { $files.Add($item.FullName) }
            if ($files.Count + $pending.Count -gt 10000) { throw 'Too many resource entries.' }
        }
    }
    foreach ($path in ($files | Sort-Object)) {
        $relative = $path.Substring($package.Length + 1).Replace('\','/')
        $lines += '{0}  {1}' -f (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    }
    AtomicText $sums (($lines -join "`n") + "`n")
}
Write-Output 'Welcome scene checksums updated. Restart ZeroSlack to load them. Check Activity > Welcome resources for schema errors.'
