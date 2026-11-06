Param(
    [Parameter(Mandatory=$false)]
    [Switch]$clean
)

# Run from this folder no matter where the script is invoked from.
Set-Location $PSScriptRoot

if ($clean.IsPresent -and (Test-Path -Path "build")) {
    Remove-Item build -Recurse -Force
}

if (-not (Test-Path -Path "build")) {
    New-Item -Path build -ItemType Directory | Out-Null
}

& cmake -G "Ninja" -DCMAKE_BUILD_TYPE="RelWithDebInfo" . -B build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build ./build
exit $LASTEXITCODE
