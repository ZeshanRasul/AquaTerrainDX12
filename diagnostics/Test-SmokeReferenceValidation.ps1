param([string]$RunRoot = "$PSScriptRoot/runs/reference-independent-review")
$ErrorActionPreference = 'Stop'
$source = Join-Path $RunRoot 'static-sl'
$root = Join-Path $PSScriptRoot ('runs/reference-validator-tests-' + [guid]::NewGuid().ToString('N'))
foreach ($fault in @('truncated','reordered','nan','l2_failure','missing_steps')) {
    $run = Join-Path $root "$fault/static-sl"
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'manifest.json') -Destination $run
    $rows = @(Import-Csv -LiteralPath (Join-Path $source 'steps.csv'))
    switch ($fault) {
        truncated { $rows = $rows[0..($rows.Count-2)] }
        reordered { $rows[1].step = '1' }
        nan { $rows[5].l1_normalized = 'NaN' }
        l2_failure { $rows[5].l2_normalized = '0.5' }
    }
    if ($fault -ne 'missing_steps') { $rows | Export-Csv -LiteralPath (Join-Path $run 'steps.csv') -NoTypeInformation }
    $rejected = $false
    try { & "$PSScriptRoot/Analyze-SmokeAdvectionReference.ps1" -RunRoot (Split-Path $run) *> $null }
    catch {
        $rejected = $_.Exception.Message -match 'Incomplete reference|misordered step|Nonfinite l1|Reference validation failed|Missing run artifact'
        if (-not $rejected) { throw }
    }
    if (-not $rejected) { throw "Validator accepted damaged data: $fault" }
    Write-Output "PASS: rejected $fault"
}
