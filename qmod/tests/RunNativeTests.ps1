param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$RunOnQuest,
    [string]$AdbPath = '',
    [string]$Serial = ''
)
$ErrorActionPreference = 'Stop'
$qmodRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path (Join-Path $outputRoot 'shim/Chat') -Force | Out-Null
$utf8 = [System.Text.UTF8Encoding]::new($false)
# Keep JSON helper bodies unchanged; replace only their Unity include chain.
$json = [System.IO.File]::ReadAllText((Join-Path $qmodRoot 'include/Chat/Json.hpp'))
$headers = '#include "rapidjson/document.h"' + [Environment]::NewLine + '#include "rapidjson/stringbuffer.h"' + [Environment]::NewLine + '#include "rapidjson/writer.h"'
[System.IO.File]::WriteAllText((Join-Path $outputRoot 'shim/Chat/Json.hpp'), $json.Replace('#include "Compat.hpp"', $headers), $utf8)
$logging = '#pragma once' + [Environment]::NewLine + '#include "fmt/format.h"' + [Environment]::NewLine + '#define INFO(...) ((void)0)' + [Environment]::NewLine + '#define DEBUG(...) ((void)0)' + [Environment]::NewLine + '#define ERROR(...) ((void)0)' + [Environment]::NewLine + '#define WARNING(...) ((void)0)'
[System.IO.File]::WriteAllText((Join-Path $outputRoot 'shim/logging.hpp'), $logging, $utf8)
# Exercise the production download queue without requiring a Unity process.
$atlas = [System.IO.File]::ReadAllText((Join-Path $qmodRoot 'src/UI/EmoteAtlas.cpp'))
$start = $atlas.IndexOf('    // One worker,')
$end = $atlas.IndexOf('    EmoteAtlas& EmoteAtlas::Instance()')
if ($start -lt 0 -or $end -le $start) { throw 'Production downloader boundaries changed.' }
$downloader = 'namespace SimpleBSStreamChat::UI { constexpr size_t MaxDownloadBytes = 2 * 1024 * 1024;' + [Environment]::NewLine + $atlas.Substring($start, $end - $start) + [Environment]::NewLine + '}'
[System.IO.File]::WriteAllText((Join-Path $outputRoot 'shim/Downloader-under-test.hpp'), $downloader, $utf8)
$ndk = ([System.IO.File]::ReadAllText((Join-Path $qmodRoot 'bs-1.40.8/ndkpath.txt'))).Trim()
$compiler = Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android29-clang++.cmd'
$includeArgs = @('-std=c++23','-O1','-DFMT_HEADER_ONLY','-I',(Join-Path $outputRoot 'shim'),'-I',(Join-Path $qmodRoot 'include'),'-I',(Join-Path $qmodRoot 'bs-1.40.8/extern/includes'),'-I',(Join-Path $qmodRoot 'bs-1.40.8/extern/includes/beatsaber-hook/shared/rapidjson/include'),'-I',(Join-Path $qmodRoot 'bs-1.40.8/extern/includes/fmt/fmt/include'))
$units = @((Join-Path $PSScriptRoot 'ChatNativeTests.cpp'))
foreach ($file in @('Chat/TwitchIrcClient.cpp','Chat/IrcMessage.cpp','Chat/ChatService.cpp','Chat/ThirdPartyEmotes.cpp','Chat/YouTubeChatClient.cpp','Chat/KickChatClient.cpp','UI/GifDecoder.cpp')) { $units += Join-Path $qmodRoot ('src/' + $file) }
$objects = @()
$compileLog = Join-Path $outputRoot 'compile.log'
Set-Content -LiteralPath $compileLog -Value ''
foreach ($unit in $units) {
    $object = Join-Path $outputRoot (([System.IO.Path]::GetFileNameWithoutExtension($unit)) + '.o')
    & $compiler @includeArgs -c $unit -o $object *>> $compileLog
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $compileLog -Tail 30; throw 'Native test compilation failed.' }
    $objects += $object
}
$binary = Join-Path $outputRoot 'sbssc-chat-native-tests'
& $compiler -static-libstdc++ -pthread @objects -o $binary *>> $compileLog
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $compileLog -Tail 30; throw 'Native test linking failed.' }
Write-Output 'Native tests compiled from production sources.'
if ($RunOnQuest) {
    if (-not $AdbPath) { $AdbPath = (Get-Command adb -ErrorAction Stop).Source }
    $deviceArgs = @(); if ($Serial) { $deviceArgs = @('-s', $Serial) }
    $remote = '/data/local/tmp/sbssc-chat-native-tests'
    & $AdbPath @deviceArgs push $binary $remote
    if ($LASTEXITCODE -ne 0) { throw 'Could not stage native tests.' }
    try {
        & $AdbPath @deviceArgs shell chmod 700 $remote
        if ($LASTEXITCODE -ne 0) { throw 'Could not mark native tests executable.' }
        $results = & $AdbPath @deviceArgs shell $remote
        $testExit = $LASTEXITCODE
        $results | Set-Content -LiteralPath (Join-Path $outputRoot 'results.txt') -Encoding utf8
        $results
        if ($testExit -ne 0) { throw ('Native tests failed: ' + $testExit) }
    } finally { & $AdbPath @deviceArgs shell rm $remote }
}
