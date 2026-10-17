[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [string]$QtPath = "C:\Qt\6.12.0\msvc2022_64",

    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Command,

        [Parameter()]
        [string[]]$Arguments = @()
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command $($Arguments -join ' ')"
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot "build"
$qtConfig = Join-Path $QtPath "lib\cmake\Qt6\Qt6Config.cmake"

if (-not (Test-Path -LiteralPath $qtConfig -PathType Leaf)) {
    throw "Qt 6 was not found at '$QtPath'. Pass -QtPath with the path to a Qt installation containing lib\cmake\Qt6\Qt6Config.cmake."
}

$QtPath = (Resolve-Path -LiteralPath $QtPath).Path

Push-Location $repositoryRoot
try {
    Invoke-CheckedCommand cmake @(
        "-S", $repositoryRoot,
        "-B", $buildDirectory,
        "-DHORUSRF_BUILD_STUDIO=ON",
        "-DCMAKE_PREFIX_PATH=$QtPath"
    )

    Invoke-CheckedCommand cmake @(
        "--build", $buildDirectory,
        "--config", $Configuration,
        "--parallel"
    )

    Invoke-CheckedCommand ctest @(
        "--test-dir", $buildDirectory,
        "-C", $Configuration,
        "--output-on-failure"
    )

    $studioCandidates = @(
        (Join-Path $buildDirectory "$Configuration\horusrf-studio.exe"),
        (Join-Path $buildDirectory "horusrf-studio.exe")
    )
    $studioPath = $studioCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1

    if (-not $studioPath) {
        throw "The build and tests succeeded, but horusrf-studio.exe was not found in the expected build output directories."
    }

    if ($NoLaunch) {
        Write-Host "Build and tests succeeded; Studio launch was skipped."
        return
    }

    Write-Host "Launching $studioPath"
    Invoke-CheckedCommand $studioPath
}
finally {
    Pop-Location
}
