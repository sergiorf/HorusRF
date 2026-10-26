[CmdletBinding()]
param(
    [string]$K1Report = "build/ai-evaluation/m6q8-controlled-structural-rag-k1-r3.json",
    [string]$K3Report = "build/ai-evaluation/m6q8-controlled-structural-rag-k3-r3.json",
    [string]$OutputPath = "build/ai-evaluation/m6q9-slice1-guardrail-replay.json",
    [string]$ThreeBModel = "qwen2.5-coder:3b",
    [string]$SevenBModel = "qwen2.5-coder:7b"
)

$ErrorActionPreference = "Stop"

function Read-EvaluationReport {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $report = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    if ($report.schema -ne "horusrf.ai-evaluation" -or $report.version -ne 3) {
        throw "Guardrail replay requires a horusrf.ai-evaluation schema-v3 report: $resolved"
    }
    return [pscustomobject]@{
        path = $resolved
        report = $report
        sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Get-CompletionProxyVerdict {
    param([Parameter(Mandatory)][object]$Case)

    if ($Case.generation_status -ne "completed" -or -not $Case.final_valid) {
        return "reject"
    }
    if ($Case.support_assessment.status -eq "supported") {
        return "allow"
    }
    return "review_required"
}

function Get-Rate {
    param([int]$Numerator, [int]$Denominator)

    if ($Denominator -eq 0) { return $null }
    return [math]::Round($Numerator / [double]$Denominator, 6)
}

function Get-GateSummary {
    param(
        [Parameter(Mandatory)][object]$Report,
        [Parameter(Mandatory)][string]$Model,
        [Parameter(Mandatory)][string]$Policy
    )

    $cases = @($Report.cases | Where-Object {
        $_.model -eq $Model -and $_.expected_supported
    })
    $allowed = @($cases | Where-Object {
        (Get-CompletionProxyVerdict -Case $_) -eq "allow"
    })
    $review = @($cases | Where-Object {
        (Get-CompletionProxyVerdict -Case $_) -eq "review_required"
    })
    $rejected = @($cases | Where-Object {
        (Get-CompletionProxyVerdict -Case $_) -eq "reject"
    })
    $correctAllowed = @($allowed | Where-Object {
        $_.requirement_verification.status -eq "satisfied"
    })

    return [pscustomobject][ordered]@{
        policy = $Policy
        model = $Model
        applicable = $cases.Count
        allowed = $allowed.Count
        review_required = $review.Count
        rejected = $rejected.Count
        correct_allowed = $correctAllowed.Count
        incorrect_allowed = $allowed.Count - $correctAllowed.Count
        accepted_precision = Get-Rate $correctAllowed.Count $allowed.Count
        verified_completion = Get-Rate $correctAllowed.Count $cases.Count
        coverage = Get-Rate $allowed.Count $cases.Count
    }
}

function Get-UniqueValues {
    param([object[]]$Values)
    return @($Values | Where-Object { $null -ne $_ -and "$_" -ne "" } | Sort-Object -Unique)
}

$k1 = Read-EvaluationReport -Path $K1Report
$k3 = Read-EvaluationReport -Path $K3Report

foreach ($field in "dataset_version", "comparison_version", "requirement_schema_version") {
    if ($k1.report.$field -ne $k3.report.$field) {
        throw "Reports differ in ${field}; replay would not be controlled."
    }
}
foreach ($field in "temperature", "max_output_tokens", "timeout_ms", "maximum_repair_attempts") {
    if ($k1.report.settings.$field -ne $k3.report.settings.$field) {
        throw "Reports differ in settings.${field}; replay would not be controlled."
    }
}
if ($k1.report.prompt_configuration_id -ne "structural-rag-v1" -or
    $k3.report.prompt_configuration_id -ne "structural-rag-v1" -or
    $k1.report.retrieval.top_k -ne 1 -or $k3.report.retrieval.top_k -ne 3) {
    throw "Expected structural-rag-v1 K=1 and K=3 reports."
}

$gateSummaries = @(
    Get-GateSummary -Report $k1.report -Model $ThreeBModel -Policy "3b-k1-completion-proxy"
    Get-GateSummary -Report $k3.report -Model $SevenBModel -Policy "7b-k3-completion-proxy"
)

$threeByRun = @{}
foreach ($case in $k1.report.cases | Where-Object { $_.model -eq $ThreeBModel }) {
    $threeByRun["$($case.case_id)|$($case.repetition)"] = $case
}
$sevenByRun = @{}
foreach ($case in $k3.report.cases | Where-Object { $_.model -eq $SevenBModel }) {
    $sevenByRun["$($case.case_id)|$($case.repetition)"] = $case
}

$applicable = 0
$accepted = 0
$correctAccepted = 0
$escalated = 0
$reviewRequired = 0
$rejected = 0
foreach ($key in @($threeByRun.Keys | Sort-Object)) {
    $three = $threeByRun[$key]
    if (-not $three.expected_supported) { continue }
    if (-not $sevenByRun.ContainsKey($key)) {
        throw "K=3 report is missing paired run $key"
    }
    ++$applicable
    if ((Get-CompletionProxyVerdict -Case $three) -eq "allow") {
        ++$accepted
        if ($three.requirement_verification.status -eq "satisfied") {
            ++$correctAccepted
        }
        continue
    }

    ++$escalated
    $seven = $sevenByRun[$key]
    switch (Get-CompletionProxyVerdict -Case $seven) {
        "allow" {
            ++$accepted
            if ($seven.requirement_verification.status -eq "satisfied") {
                ++$correctAccepted
            }
        }
        "review_required" { ++$reviewRequired }
        default { ++$rejected }
    }
}

$cascade = [pscustomobject][ordered]@{
    policy = "3b-k1-to-7b-k3-completion-proxy"
    applicable = $applicable
    accepted = $accepted
    correct_accepted = $correctAccepted
    incorrect_accepted = $accepted - $correctAccepted
    escalated = $escalated
    review_required = $reviewRequired
    rejected = $rejected
    accepted_precision = Get-Rate $correctAccepted $accepted
    verified_completion = Get-Rate $correctAccepted $applicable
    coverage = Get-Rate $accepted $applicable
    escalation_rate = Get-Rate $escalated $applicable
}

$unsuccessful = @($k3.report.cases | Where-Object {
    $_.model -eq $SevenBModel -and $_.expected_supported -and
    $_.requirement_verification.status -ne "satisfied"
})
$failureTaxonomy = @(
    $unsuccessful |
        Group-Object -Property case_id |
        Sort-Object -Property Name |
        ForEach-Object {
            $items = @($_.Group)
            $attempts = @($items | ForEach-Object { $_.attempts })
            $lastAttempts = @($items | ForEach-Object { @($_.attempts)[-1] })
            $initialAttempts = @($items | ForEach-Object { @($_.attempts)[0] })
            $boundary = if (@($items | Where-Object {
                    $_.generation_status -eq "completed" -and $_.final_valid
                }).Count -eq $items.Count) {
                "interactive_acceptance_gap"
            } elseif (@($items | Where-Object { $_.final_valid }).Count -eq $items.Count) {
                "requirement_correction_no_progress"
            } elseif (@($items | Where-Object { -not $_.final_valid }).Count -eq $items.Count) {
                "compiler_repair_no_progress"
            } else {
                "mixed"
            }
            [pscustomobject][ordered]@{
                case_id = $_.Name
                category = $items[0].category
                expected_support = $items[0].expected_support
                repetitions = $items.Count
                first_failing_boundary = $boundary
                terminal_error_codes = @(Get-UniqueValues @($items | ForEach-Object { $_.error.code }))
                purpose_sequences = @(Get-UniqueValues @($items | ForEach-Object {
                    @($_.attempts | ForEach-Object { $_.purpose }) -join ">"
                }))
                compiler_diagnostic_codes = @(Get-UniqueValues @($lastAttempts | ForEach-Object {
                    $_.compiler_diagnostics | ForEach-Object { $_.code }
                }))
                unsatisfied_requirement_codes = @(Get-UniqueValues @($items | ForEach-Object {
                    $_.requirement_verification.outcomes |
                        Where-Object { $_.status -ne "satisfied" } |
                        ForEach-Object { $_.code }
                }))
                selected_retrieval_ids = @(Get-UniqueValues @($initialAttempts | ForEach-Object {
                    $_.retrieval.selected | ForEach-Object { $_.id }
                }))
                exact_candidate_repetitions = @($attempts | Where-Object {
                    $_.repeated_candidate
                }).Count
                unused_ranked_alternative_observable = $false
            }
        }
)

$output = [pscustomobject][ordered]@{
    schema = "horusrf.ai-guardrail-replay"
    version = 1
    inputs = [pscustomobject][ordered]@{
        k1 = [pscustomobject][ordered]@{
            report_file = [IO.Path]::GetFileName($k1.path)
            sha256 = $k1.sha256
            git_revision = $k1.report.git_revision
        }
        k3 = [pscustomobject][ordered]@{
            report_file = [IO.Path]::GetFileName($k3.path)
            sha256 = $k3.sha256
            git_revision = $k3.report.git_revision
        }
        dataset_version = $k1.report.dataset_version
        comparison_version = $k1.report.comparison_version
        requirement_schema_version = $k1.report.requirement_schema_version
    }
    limitations = @(
        "Schema-v3 reports do not retain the final interactive requirement-verification verdict; agent completion is only a proxy for the proposed Apply gate.",
        "Reports retain selected retrieval examples but not the ranked candidates beyond K, so alternate-rescue availability cannot be established from replay.",
        "Cascade quality is replayed from paired runs; latency, cache behavior, and model-load effects are not measured."
    )
    gate_replay = $gateSummaries
    cascade_replay = $cascade
    seven_b_k3_unsuccessful_runs = $unsuccessful.Count
    failure_taxonomy = $failureTaxonomy
}

$resolvedOutput = [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
$outputDirectory = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$output | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resolvedOutput -Encoding UTF8
Write-Output $resolvedOutput
