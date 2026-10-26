[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$RepositoryRoot,

    [Parameter(Mandatory)]
    [string]$WorkDirectory
)

$ErrorActionPreference = "Stop"

function Assert-Equal {
    param($Actual, $Expected, [string]$Message)

    if ($Actual -ne $Expected) {
        throw "$Message (expected '$Expected', got '$Actual')"
    }
}

New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null
$archiveScript = Join-Path $RepositoryRoot "scripts\archive-ai-evaluation.ps1"
foreach ($name in @(
    "schema-v4-report.json",
    "schema-v4-snapshot.json",
    "schema-v3-report.json",
    "schema-v3-snapshot.json")) {
    $path = Join-Path $WorkDirectory $name
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

$case = [pscustomobject][ordered]@{
    model = "test-model"
    case_id = "named-output"
    category = "measurement"
    expected_supported = $true
    generation_status = "completed"
    first_attempt_valid = $true
    final_valid = $true
    repair_attempts = 0
    total_latency_ms = 10
    requirement_verification = [pscustomobject]@{ status = "satisfied" }
    interactive_requirement_verification = [pscustomobject]@{ status = "satisfied" }
    acceptance_decision = [pscustomobject]@{
        verdict = "allow"
        reason_codes = @("interactive_requirements_satisfied")
    }
    semantic_status = "match"
    unsupported_recognized = $false
    provider_identity = [pscustomobject]@{
        provider = "scripted"
        model_digest = "fixture"
    }
}
$report = [pscustomobject][ordered]@{
    schema = "horusrf.ai-evaluation"
    version = 4
    timestamp_utc = "2026-10-26T12:00:00Z"
    git_revision = "fixture-revision"
    dataset_version = "fixture"
    comparison_version = "semantic-ir-v2"
    requirement_schema_version = "golden-requirements-v2"
    prompt_configuration_id = "direct-dsl-v1"
    settings = [pscustomobject]@{ repetitions = 1 }
    retrieval = $null
    cases = @($case)
    summaries = @([pscustomobject]@{ model = "test-model" })
}

$v4Path = Join-Path $WorkDirectory "schema-v4-report.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $v4Path -Encoding UTF8
$v4SnapshotPath = & $archiveScript -ReportPath $v4Path `
    -OutputDirectory $WorkDirectory -SnapshotName "schema-v4-snapshot.json"
$v4Snapshot = Get-Content -LiteralPath $v4SnapshotPath -Raw | ConvertFrom-Json

Assert-Equal $v4Snapshot.source.evaluation_schema_version 4 "Schema v4 was not retained"
Assert-Equal $v4Snapshot.cases[0].interactive_requirement_satisfied 1 "Interactive success was not archived"
Assert-Equal $v4Snapshot.cases[0].interactive_requirement_violated 0 "Interactive violation count is wrong"
Assert-Equal $v4Snapshot.cases[0].acceptance_allowed 1 "Allow count was not archived"
Assert-Equal $v4Snapshot.cases[0].acceptance_review_required 0 "Review count is wrong"
Assert-Equal $v4Snapshot.cases[0].acceptance_rejected 0 "Reject count is wrong"
Assert-Equal $v4Snapshot.cases[0].accepted_requirement_satisfied 1 "Accepted precision numerator was not archived"

$report.version = 3
$v3Path = Join-Path $WorkDirectory "schema-v3-report.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $v3Path -Encoding UTF8
$v3SnapshotPath = & $archiveScript -ReportPath $v3Path `
    -OutputDirectory $WorkDirectory -SnapshotName "schema-v3-snapshot.json"
$v3Snapshot = Get-Content -LiteralPath $v3SnapshotPath -Raw | ConvertFrom-Json

Assert-Equal $v3Snapshot.source.evaluation_schema_version 3 "Schema v3 compatibility failed"
if ($null -ne $v3Snapshot.cases[0].interactive_requirement_satisfied -or
    $null -ne $v3Snapshot.cases[0].acceptance_allowed) {
    throw "Schema v3 acceptance fields must remain unavailable rather than becoming zeroes."
}
