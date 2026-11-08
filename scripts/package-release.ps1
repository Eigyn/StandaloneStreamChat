[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('1.40.8', '1.45.3')]
    [string]$GameVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target = if ($GameVersion -eq '1.40.8') {
    @{ Qmod = 'bs-1.40.8/StandaloneStreamChat-1.40.8.qmod'; PackageVersion = '1.40.8_7379' }
} else {
    @{ Qmod = 'StandaloneStreamChat-1.45.3.qmod'; PackageVersion = '1.45.3_30054' }
}
$qmodPath = Join-Path $projectRoot $target.Qmod
$packageRoot = Split-Path -Parent $qmodPath
$package = Get-Content -LiteralPath (Join-Path $packageRoot 'qpm.json') -Raw | ConvertFrom-Json
$binaryName = [string]$package.info.additionalData.overrideSoName
$binaryPath = Join-Path $packageRoot "build/$binaryName"
$expectedBinaryHash = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash
$licenseDirectory = Join-Path $projectRoot 'licenses'
$noticeFiles = @(
    Get-Item -LiteralPath (Join-Path $projectRoot 'LICENSE')
    Get-Item -LiteralPath (Join-Path $projectRoot 'THIRD-PARTY-NOTICES')
    Get-ChildItem -LiteralPath $licenseDirectory -File | Sort-Object Name
)
foreach ($required in @(
    'LICENSE.QuestChatPlexSDK-BS', 'LICENSE.And64InlineHook', 'LICENSE.OpenSSL',
    'LICENSE.libcurl', 'LICENSE.RapidJSON', 'LICENSE.fmt', 'LICENSE.utfcpp',
    'NOTICE.Quest-BSML', 'NOTICE.MetaCoreBS', 'UPSTREAM-SOURCES.json'
)) {
    if ($required -notin $noticeFiles.Name) {
        throw "Missing required third-party notice: $required"
    }
}
if (@($noticeFiles.Name | Sort-Object -Unique).Count -ne $noticeFiles.Count) {
    throw 'License filenames must be unique across the project root and licenses/.'
}

# mod.json names this as its coverImage, and BSQMods finds the same file at
# the repository root.
$coverPath = Join-Path $projectRoot 'cover.png'
if (-not (Test-Path -LiteralPath $coverPath -PathType Leaf)) {
    throw 'Missing cover.png in the project root.'
}
$coverFile = Get-Item -LiteralPath $coverPath
$archiveFiles = @($noticeFiles) + $coverFile

# Keep the notices inside the installable archive as well as beside it, so a
# QMOD shared separately from the release ZIP still carries the notices.
$qmod = [System.IO.Compression.ZipFile]::Open($qmodPath, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    $manifestEntry = $qmod.GetEntry('mod.json')
    if (-not $manifestEntry) { throw 'The QMOD has no mod.json.' }
    $reader = [System.IO.StreamReader]::new($manifestEntry.Open())
    try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    # 0.10.0 moved the package ID from SimpleBSStreamChat to StandaloneStreamChat.
    if ($manifest.id -ne 'StandaloneStreamChat' -or $manifest.name -ne 'StandaloneStreamChat' -or
        $manifest.packageVersion -ne $target.PackageVersion) {
        throw "The QMOD does not match the $GameVersion release target."
    }
    $version = [string]$manifest.version
    if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?$') {
        throw "Invalid release version in QMOD: $version"
    }
    if ($version -ne $package.info.version) {
        throw "QMOD version $version does not match the project version $($package.info.version)."
    }
    $cover = $manifest.PSObject.Properties['coverImage']
    if (-not $cover -or $cover.Value -cne $coverFile.Name) {
        throw "mod.json must name $($coverFile.Name) as its coverImage."
    }
    $libraries = @($qmod.Entries | Where-Object FullName -CEQ $binaryName)
    if (@($manifest.lateModFiles).Count -ne 1 -or $manifest.lateModFiles[0] -cne $binaryName -or $libraries.Count -ne 1) {
        throw "Expected exactly one late mod library named $binaryName."
    }
    $stream = $libraries[0].Open()
    try { $archivedBinaryHash = [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($stream)) }
    finally { $stream.Dispose() }
    if ($archivedBinaryHash -ne $expectedBinaryHash) {
        throw "The QMOD library does not match $binaryPath."
    }
    foreach ($file in $archiveFiles) {
        foreach ($old in @($qmod.Entries | Where-Object FullName -EQ $file.Name)) {
            $old.Delete()
        }
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $qmod, $file.FullName, $file.Name, [System.IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
    }
} finally {
    $qmod.Dispose()
}

# Check the actual archive bytes before writing release artifacts.
$qmod = [System.IO.Compression.ZipFile]::OpenRead($qmodPath)
try {
    foreach ($file in $archiveFiles) {
        $entry = $qmod.GetEntry($file.Name)
        if (-not $entry) { throw "The QMOD is missing $($file.Name)." }
        $stream = $entry.Open()
        try {
            $archivedHash = [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($stream))
        } finally { $stream.Dispose() }
        if ($archivedHash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
            throw "The QMOD copy does not match its source: $($file.Name)"
        }
    }
} finally {
    $qmod.Dispose()
}

$releaseRoot = Join-Path $projectRoot 'releases'
$releaseName = "StandaloneStreamChat-$version-bs-$GameVersion"
$releaseFolder = Join-Path $releaseRoot $releaseName
New-Item -ItemType Directory -Path $releaseFolder -Force | Out-Null
$releaseFiles = @(
    foreach ($file in @((Get-Item -LiteralPath $qmodPath)) + $noticeFiles) {
        $destination = Join-Path $releaseFolder $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        Get-Item -LiteralPath $destination
    }
)

# Archive the explicit release file list. Other files a user may have placed
# in the output folder are preserved and cannot accidentally enter the ZIP.
$releaseZip = Join-Path $releaseRoot "$releaseName.zip"
$zipStream = [System.IO.File]::Open($releaseZip, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
    $zip = [System.IO.Compression.ZipArchive]::new($zipStream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($file in $releaseFiles) {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $file.FullName, "$releaseName/$($file.Name)", [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    } finally { $zip.Dispose() }
} finally {
    $zipStream.Dispose()
}

Write-Output "Packaged $($noticeFiles.Count) license/provenance files and $($coverFile.Name) with the $GameVersion QMOD."
Write-Output "Release folder: $releaseFolder"
Write-Output "Release ZIP: $releaseZip"
