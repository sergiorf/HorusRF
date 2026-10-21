[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string]$Model = "qwen2.5-coder:7b",

    [ValidateSet("Auto", "Gpu", "Cpu")]
    [string]$Processor = "Gpu",

    [switch]$RestartServer,

    [switch]$Pull,

    [ValidateRange(1, 300)]
    [int]$StartupTimeoutSeconds = 30,

    [ValidateRange(1, 1800)]
    [int]$LoadTimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"
$endpoint = "http://127.0.0.1:11434"

function Invoke-OllamaApi {
    param(
        [Parameter(Mandatory)]
        [ValidateSet("Get", "Post")]
        [string]$Method,

        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter()]
        [object]$Body,

        [Parameter()]
        [int]$TimeoutSeconds = 2
    )

    $parameters = @{
        Uri = "$endpoint$Path"
        Method = $Method
        TimeoutSec = $TimeoutSeconds
    }
    if ($PSBoundParameters.ContainsKey("Body")) {
        $parameters.ContentType = "application/json"
        $parameters.Body = $Body | ConvertTo-Json -Compress
    }

    Invoke-RestMethod @parameters
}

function Test-OllamaReady {
    try {
        $null = Invoke-OllamaApi -Method Get -Path "/api/tags"
        return $true
    }
    catch {
        return $false
    }
}

function Start-OllamaServer {
    param(
        [Parameter(Mandatory)]
        [System.Management.Automation.ApplicationInfo]$Command,

        [Parameter(Mandatory)]
        [ValidateSet("Auto", "Gpu", "Cpu")]
        [string]$Processor
    )

    $previousValues = @{}
    $overrides = @{}
    if ($Processor -eq "Cpu") {
        $overrides = @{
            CUDA_VISIBLE_DEVICES = "-1"
            ROCR_VISIBLE_DEVICES = "-1"
            GGML_VK_VISIBLE_DEVICES = "-1"
        }
    }
    elseif ($Processor -eq "Gpu") {
        # Ollama drops integrated GPUs by default. Explicit GPU mode opts in so
        # Intel Arc and other supported integrated Vulkan devices are considered.
        $overrides = @{
            OLLAMA_IGPU_ENABLE = "1"
        }
    }

    try {
        foreach ($name in $overrides.Keys) {
            $previousValues[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
            [Environment]::SetEnvironmentVariable($name, $overrides[$name], "Process")
        }

        $arguments = @{
            FilePath = $Command.Source
            ArgumentList = "serve"
            PassThru = $true
            WindowStyle = "Hidden"
        }
        return Start-Process @arguments
    }
    finally {
        foreach ($name in $overrides.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previousValues[$name], "Process")
        }
    }
}

function Stop-LocalOllama {
    param(
        [Parameter(Mandatory)]
        [System.Management.Automation.ApplicationInfo]$Command
    )

    $installationDirectory = Split-Path -Parent $Command.Source
    $ollamaProcesses = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        try {
            $_.Path -and
                (Split-Path -Parent $_.Path) -eq $installationDirectory -and
                $_.ProcessName -like "ollama*"
        }
        catch {
            $false
        }
    })

    foreach ($process in ($ollamaProcesses | Sort-Object { $_.ProcessName -eq "ollama" })) {
        $currentProcess = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if ($currentProcess) {
            Stop-Process -Id $currentProcess.Id -Force -ErrorAction Stop
        }
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (Test-OllamaReady) {
        if ([DateTime]::UtcNow -ge $deadline) {
            throw "The existing local Ollama server did not stop within 10 seconds."
        }
        Start-Sleep -Milliseconds 250
    }
}

$ollama = Get-Command ollama -CommandType Application -ErrorAction SilentlyContinue
if (-not $ollama) {
    throw "Ollama was not found on PATH. Install it from https://ollama.com/download and open a new terminal."
}

$serverProcess = $null
$serverWasRunning = Test-OllamaReady
if ($serverWasRunning -and $RestartServer) {
    Write-Host "Stopping the existing local Ollama app and server."
    Stop-LocalOllama -Command $ollama
    $serverWasRunning = $false
}

if ($serverWasRunning) {
    if ($Processor -eq "Cpu") {
        throw "Ollama is already running, so CPU-only mode cannot be applied safely. Re-run with -Processor Cpu -RestartServer."
    }
    Write-Host "Using the Ollama server already running at $endpoint."
}
else {
    $modeDescription = switch ($Processor) {
        "Cpu" { "CPU-only mode" }
        "Gpu" { "GPU-required mode (including supported integrated GPUs)" }
        default { "automatic hardware selection" }
    }
    Write-Host "Starting Ollama at $endpoint with $modeDescription."
    $serverProcess = Start-OllamaServer -Command $ollama -Processor $Processor

    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while (-not (Test-OllamaReady)) {
        if ($serverProcess.HasExited) {
            throw "Ollama exited with code $($serverProcess.ExitCode) before its API became ready."
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            throw "Ollama did not become ready within $StartupTimeoutSeconds seconds."
        }
        Start-Sleep -Milliseconds 250
    }
}

$tags = Invoke-OllamaApi -Method Get -Path "/api/tags"
$installedModels = @($tags.models | ForEach-Object {
    if ($_.name) { $_.name }
    elseif ($_.model) { $_.model }
})

if ($installedModels -notcontains $Model) {
    if (-not $Pull) {
        throw "Model '$Model' is not installed. Re-run with -Pull to download it explicitly."
    }

    Write-Host "Pulling model '$Model'."
    & $ollama.Source pull $Model
    if ($LASTEXITCODE -ne 0) {
        throw "ollama pull failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Loading model '$Model'."
$loadRequest = @{
    model = $Model
    prompt = ""
    stream = $false
    keep_alive = "10m"
}
$null = Invoke-OllamaApi -Method Post -Path "/api/generate" -Body $loadRequest -TimeoutSeconds $LoadTimeoutSeconds

$running = Invoke-OllamaApi -Method Get -Path "/api/ps"
$loadedModel = @($running.models | Where-Object {
    $_.name -eq $Model -or $_.model -eq $Model
}) | Select-Object -First 1
if (-not $loadedModel) {
    throw "Ollama accepted the load request, but '$Model' is not present in /api/ps."
}

$gpuBytes = if ($null -eq $loadedModel.size_vram) { 0 } else { [Int64]$loadedModel.size_vram }
if ($Processor -eq "Gpu" -and $gpuBytes -le 0) {
    if ($serverWasRunning) {
        throw "The existing Ollama server loaded '$Model' without GPU memory. GPU selection is fixed when the server starts; re-run with -Processor Gpu -RestartServer."
    }
    throw "Model '$Model' loaded without GPU memory. Check Ollama's GPU compatibility and driver requirements, or use -Processor Auto or Cpu."
}
if ($Processor -eq "Cpu" -and $gpuBytes -gt 0) {
    throw "Model '$Model' still reports GPU memory in CPU-only mode. Check the Ollama server configuration."
}

Write-Host "Ollama is ready with '$Model' (kept loaded for 10 minutes)."
& $ollama.Source ps
if ($LASTEXITCODE -ne 0) {
    Write-Warning "The model loaded successfully, but 'ollama ps' failed with exit code $LASTEXITCODE."
}
