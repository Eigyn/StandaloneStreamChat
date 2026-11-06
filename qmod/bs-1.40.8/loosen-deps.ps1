# Runs after `qpm qmod zip` (which regenerates mod.json, so this can't go
# before it) and rewrites the manifest inside the finished .qmod.
#
# The build pins each library to the exact 1.40.8 core-mod version (the
# oldest any 1.40.8 setup has, so nothing we link against is missing there),
# but players' setups often carry newer compatible releases, such as
# custom-types 0.18.4 for BS+ and BetterSongSearch. An exact pin in mod.json
# makes MBF try to move that library under every other mod and refuse to
# install, so the manifest accepts any newer release instead: "=x.y.z"
# becomes "^x.y.z".
Param(
    [string]$qmod = "SimpleBSStreamChat-1.40.8.qmod"
)
Set-Location $PSScriptRoot

$manifest = Get-Content mod.json -Raw | ConvertFrom-Json
foreach ($dependency in $manifest.dependencies) {
    if ($dependency.version.StartsWith('=')) {
        $dependency.version = '^' + $dependency.version.Substring(1)
    }
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content mod.json -Encoding utf8NoBOM

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open((Resolve-Path $qmod), [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $old = $archive.GetEntry('mod.json')
    if ($old) { $old.Delete() }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Resolve-Path mod.json), 'mod.json') | Out-Null
} finally {
    $archive.Dispose()
}
Write-Host "Loosened dependency ranges in $qmod"
