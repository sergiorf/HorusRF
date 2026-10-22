[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [ValidateNotNullOrEmpty()]
    [string]$Models = "qwen2.5-coder:3b,qwen2.5-coder:7b",

    [string]$Dataset = "tests/ai/golden",
    [string]$OutputPath,
    [string]$CaseIds,

    [ValidateRange(1, 100)]
    [int]$Repeat = 1,

    [ValidateRange(0.0, 10.0)]
    [double]$Temperature = 0.0,

    [ValidateRange(1, 86400)]
    [int]$TimeoutSeconds = 120,

    [ValidateRange(1, 1048576)]
    [int]$MaxOutputTokens = 2048,

    [ValidateRange(1, 20)]
    [int]$MaxRepairs = 3,

    [ValidateSet("direct-dsl-v1", "direct-dsl-compact-v1", "direct-dsl-relevant-v1")]
    [string]$PromptConfiguration = "direct-dsl-v1",

    [ValidateNotNullOrEmpty()]
    [string]$Endpoint = "http://127.0.0.1:11434",

    [switch]$SkipBuild,
    [switch]$Archive,
    [string]$ArchiveDirectory
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot "build"

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

Push-Location $repositoryRoot
try {
    if (-not $SkipBuild) {
        Invoke-CheckedCommand cmake @(
            "-S", $repositoryRoot,
            "-B", $buildDirectory
        )
        Invoke-CheckedCommand cmake @(
            "--build", $buildDirectory,
            "--config", $Configuration,
            "--target", "horusrf-ai-eval",
            "--parallel"
        )
    }

    $executableCandidates = @(
        (Join-Path $buildDirectory "$Configuration\horusrf-ai-eval.exe"),
        (Join-Path $buildDirectory "horusrf-ai-eval.exe"),
        (Join-Path $buildDirectory "$Configuration\horusrf-ai-eval"),
        (Join-Path $buildDirectory "horusrf-ai-eval")
    )
    $executable = $executableCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if (-not $executable) {
        throw "horusrf-ai-eval was not found in the expected build output directories. Configure the existing build/ directory first."
    }

    if (-not $OutputPath) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $OutputPath = Join-Path $buildDirectory "ai-evaluation\results-$stamp.json"
    }

    $arguments = @(
        "--models", $Models,
        "--dataset", $Dataset,
        "--output", $OutputPath,
        "--repeat", $Repeat,
        "--temperature", $Temperature.ToString([Globalization.CultureInfo]::InvariantCulture),
        "--timeout-seconds", $TimeoutSeconds,
        "--max-output-tokens", $MaxOutputTokens,
        "--max-repairs", $MaxRepairs,
        "--prompt-configuration", $PromptConfiguration,
        "--endpoint", $Endpoint
    )
    if ($CaseIds) {
        $arguments += @("--case", $CaseIds)
    }

    $gitCommand = Get-Command git -CommandType Application -ErrorAction SilentlyContinue
    if ($gitCommand) {
        $gitRevision = & $gitCommand.Source -C $repositoryRoot rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $gitRevision) {
            $arguments += @("--git-revision", $gitRevision.Trim())
        }
    }

    Invoke-CheckedCommand $executable $arguments

    if ($Archive) {
        $archiveArguments = @{
            ReportPath = $OutputPath
        }
        if ($ArchiveDirectory) {
            $archiveArguments.OutputDirectory = $ArchiveDirectory
        }
        & (Join-Path $PSScriptRoot "archive-ai-evaluation.ps1") @archiveArguments
    }
}
finally {
    Pop-Location
}
