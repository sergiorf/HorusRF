[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$ReportPath,

    [string]$OutputDirectory,
    [string]$SnapshotName
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Get-Count {
    param(
        [Parameter(Mandatory)]
        [object[]]$Items,
        [Parameter(Mandatory)]
        [scriptblock]$Predicate
    )

    return @($Items | Where-Object $Predicate).Count
}

function Get-Median {
    param([long[]]$Values)

    if ($Values.Count -eq 0) {
        return $null
    }

    $ordered = @($Values | Sort-Object)
    $middle = [math]::Floor($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) {
        return $ordered[$middle]
    }

    return [long][math]::Round(
        ($ordered[$middle - 1] + $ordered[$middle]) / 2.0,
        0,
        [MidpointRounding]::AwayFromZero)
}

$resolvedReport = (Resolve-Path -LiteralPath $ReportPath).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot "evaluation\history"
}

$report = Get-Content -LiteralPath $resolvedReport -Raw | ConvertFrom-Json
if ($report.schema -ne "horusrf.ai-evaluation" -or $report.version -notin @(1, 2, 3, 4)) {
    throw "Only horusrf.ai-evaluation schema versions 1, 2, 3, or 4 can be archived."
}
if (-not $report.cases -or -not $report.summaries) {
    throw "The evaluation report has no cases or summaries."
}

$revision = if ($report.git_revision) { $report.git_revision } else { "unknown-revision" }
$shortRevision = $revision.Substring(0, [math]::Min(12, $revision.Length))
$safeTimestamp = $report.timestamp_utc -replace "[-:]", "" -replace "Z$", "Z"
$configurationIdentity = if ($report.prompt_configuration_id) {
    $report.prompt_configuration_id
} else {
    "legacy-schema-v$($report.version)"
}
$safeConfiguration = $configurationIdentity -replace "[^A-Za-z0-9._-]", "-"
if (-not $SnapshotName) {
    $SnapshotName = "$safeTimestamp-$safeConfiguration-$shortRevision.json"
}
if ([IO.Path]::GetFileName($SnapshotName) -ne $SnapshotName -or
    [IO.Path]::GetExtension($SnapshotName) -ne ".json") {
    throw "SnapshotName must be a JSON file name without a directory."
}

$modelIdentities = @(
    $report.cases |
        Group-Object -Property model |
        Sort-Object -Property Name |
        ForEach-Object {
            $identities = @(
                $_.Group |
                    ForEach-Object { $_.provider_identity } |
                    Where-Object { $_ } |
                    ForEach-Object { "$($_.provider)|$($_.model_digest)" } |
                    Sort-Object -Unique
            )
            [pscustomobject][ordered]@{
                model = $_.Name
                identities = @(
                    $identities | ForEach-Object {
                        $parts = $_ -split "\|", 2
                        [pscustomobject][ordered]@{
                            provider = $parts[0]
                            model_digest = $parts[1]
                        }
                    }
                )
            }
        }
)

$caseTrends = @(
    $report.cases |
        Group-Object -Property model, case_id |
        ForEach-Object {
            $items = @($_.Group)
            $first = $items[0]
            $latencies = @($items | ForEach-Object { [long]$_.total_latency_ms })
            $repairTotal = ($items | Measure-Object -Property repair_attempts -Sum).Sum
            $hasRequirementVerification = $report.version -ge 2
            $hasAcceptanceDecision = $report.version -ge 4
            [pscustomobject][ordered]@{
                model = $first.model
                case_id = $first.case_id
                category = $first.category
                expected_supported = [bool]$first.expected_supported
                repetitions = $items.Count
                generation_completed = Get-Count $items { $_.generation_status -eq "completed" }
                first_attempt_valid = Get-Count $items { $_.first_attempt_valid }
                final_valid = Get-Count $items { $_.final_valid }
                requirement_satisfied = if ($hasRequirementVerification) {
                    Get-Count $items { $_.requirement_verification.status -eq "satisfied" }
                } else { $null }
                requirement_violated = if ($hasRequirementVerification) {
                    Get-Count $items { $_.requirement_verification.status -eq "violated" }
                } else { $null }
                requirement_unknown = if ($hasRequirementVerification) {
                    Get-Count $items { $_.requirement_verification.status -eq "unknown" }
                } else { $null }
                interactive_requirement_satisfied = if ($hasAcceptanceDecision) {
                    Get-Count $items { $_.interactive_requirement_verification.status -eq "satisfied" }
                } else { $null }
                interactive_requirement_violated = if ($hasAcceptanceDecision) {
                    Get-Count $items { $_.interactive_requirement_verification.status -eq "violated" }
                } else { $null }
                interactive_requirement_unknown = if ($hasAcceptanceDecision) {
                    Get-Count $items {
                        $_.interactive_requirement_verification.status -in @("unknown", "not_evaluated")
                    }
                } else { $null }
                acceptance_allowed = if ($hasAcceptanceDecision) {
                    Get-Count $items { $_.acceptance_decision.verdict -eq "allow" }
                } else { $null }
                acceptance_review_required = if ($hasAcceptanceDecision) {
                    Get-Count $items { $_.acceptance_decision.verdict -eq "review_required" }
                } else { $null }
                acceptance_rejected = if ($hasAcceptanceDecision) {
                    Get-Count $items { $_.acceptance_decision.verdict -eq "reject" }
                } else { $null }
                accepted_requirement_satisfied = if ($hasAcceptanceDecision) {
                    Get-Count $items {
                        $_.acceptance_decision.verdict -eq "allow" -and
                        $_.requirement_verification.status -eq "satisfied"
                    }
                } else { $null }
                semantic_matches = Get-Count $items { $_.semantic_status -eq "match" }
                unsupported_recognized = Get-Count $items { $_.unsupported_recognized }
                average_repair_attempts = [math]::Round($repairTotal / $items.Count, 6)
                median_latency_ms = Get-Median $latencies
            }
        } |
        Sort-Object -Property model, case_id
)

$snapshot = [pscustomobject][ordered]@{
    schema = "horusrf.ai-evaluation-history"
    version = 2
    source = [pscustomobject][ordered]@{
        report_file = [IO.Path]::GetFileName($resolvedReport)
        sha256 = (Get-FileHash -LiteralPath $resolvedReport -Algorithm SHA256).Hash.ToLowerInvariant()
        evaluation_schema_version = $report.version
    }
    run = [pscustomobject][ordered]@{
        timestamp_utc = $report.timestamp_utc
        git_revision = $report.git_revision
        dataset_version = $report.dataset_version
        comparison_version = $report.comparison_version
        requirement_schema_version = $report.requirement_schema_version
        prompt_configuration_id = $report.prompt_configuration_id
        settings = $report.settings
        retrieval = $report.retrieval
        models = $modelIdentities
    }
    summaries = @($report.summaries)
    cases = $caseTrends
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputPath = Join-Path $OutputDirectory $SnapshotName
if (Test-Path -LiteralPath $outputPath) {
    throw "Refusing to overwrite existing evaluation history: $outputPath"
}

$snapshot | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding UTF8
Write-Output $outputPath
