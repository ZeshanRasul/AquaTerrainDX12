param([string]$RunRoot = "$PSScriptRoot/../experiments/mass-budget-audit/data/verified")
$ErrorActionPreference = 'Stop'
$summary = @()
foreach ($mode in @('sl','maccormack')) {
    $run = Join-Path $RunRoot "$mode-audit"
    $manifest = Get-Content -LiteralPath (Join-Path $run 'manifest.json') -Raw | ConvertFrom-Json
    $rows = @(Import-Csv -LiteralPath (Join-Path $run 'budget.csv'))
    $fields = @(Import-Csv -LiteralPath (Join-Path $run 'end-fields.csv'))
    $control = @(Import-Csv -LiteralPath (Join-Path $RunRoot "$mode-control/end-fields.csv"))
    if (-not $manifest.completed -or $manifest.audit_failed_steps -ne 0 -or $rows.Count -ne 480 -or $fields.Count -ne 480 -or $control.Count -ne 480) {
        throw "Incomplete or failed run: $run"
    }
    $differences = @(Compare-Object $fields $control -Property step,density_hash_fnv1a64,kinetic_energy,rms_divergence_after)
    if ($differences.Count) { throw "Probes changed the trajectory: $mode" }
    $previous = 0.0
    $maxDampingResidual = 0.0
    $maxLimiterResidual = 0.0
    for ($i = 0; $i -lt $rows.Count; ++$i) {
        $row = $rows[$i]
        if ([int]$row.step -ne $i + 1 -or [double]$row.before_source -ne $previous) { throw "Broken step continuity: $mode at $i" }
        if ([int]$row.emitter -ne [int]($i -lt 240)) { throw "Broken source schedule: $mode at $i" }
        if ([int]$row.audit_failed -ne 0 -or [int]$row.readback_match -ne 1) { throw "Failed probes: $mode at $i" }
        if ([double]$row.final -ne [double]$fields[$i].density_sum) { throw "Mismatched final CSV: $mode at $i" }
        $previous = [double]$row.final
        $maxDampingResidual = [Math]::Max($maxDampingResidual, [Math]::Abs([double]$row.damping_residual))
        $maxLimiterResidual = [Math]::Max($maxLimiterResidual, [Math]::Abs([double]$row.limiter_residual))
    }
    foreach ($stop in @(240,480)) {
        $window = $rows[0..($stop-1)]
        $sum = { param($column) [double](($window | Measure-Object -Property $column -Sum).Sum) }
        $entry = [ordered]@{
            mode = $mode
            through_step = $stop
            measured_injection = & $sum 'measured_injection'
            forward_delta = & $sum 'forward_delta'
            correction_delta = & $sum 'correction_delta'
            lower_limiter_delta = & $sum 'lower_delta'
            upper_limiter_delta = & $sum 'upper_delta'
            damping_and_positivity_delta = & $sum 'damping_and_positivity_delta'
            final_density_sum = [double]$rows[$stop-1].final
            ratio_to_120_injected = [double]$rows[$stop-1].final / 120.0
            max_abs_damping_residual_all_steps = $maxDampingResidual
            max_abs_limiter_residual_all_steps = $maxLimiterResidual
            control_hashes_match = $true
        }
        $reconstructed = $entry.measured_injection + $entry.forward_delta + $entry.correction_delta +
            $entry.lower_limiter_delta + $entry.upper_limiter_delta + $entry.damping_and_positivity_delta
        $entry.budget_closure_error = $entry.final_density_sum - $reconstructed
        if ([Math]::Abs($entry.budget_closure_error) -gt 1e-4) { throw "Budget does not close: $mode through $stop" }
        $summary += [pscustomobject]$entry
    }
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $RunRoot 'summary.json')
$summary | Format-Table mode,through_step,final_density_sum,ratio_to_120_injected,budget_closure_error,control_hashes_match
