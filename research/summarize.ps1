param(
    [Parameter(Mandatory = $true)]
    [string] $RawPath,
    [Parameter(Mandatory = $true)]
    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture

function Get-Quantile([double[]] $Values, [double] $Probability) {
    if ($Values.Count -eq 0) { throw 'Cannot calculate a quantile of an empty sample.' }
    [array]::Sort($Values)
    $position = ($Values.Count - 1) * $Probability
    $lower = [int][Math]::Floor($position)
    $upper = [int][Math]::Ceiling($position)
    return $Values[$lower] + ($Values[$upper] - $Values[$lower]) * ($position - $lower)
}

if (Test-Path -LiteralPath $OutputPath) { throw "Output already exists: $OutputPath" }
$rows = @(Import-Csv -LiteralPath $RawPath)
if ($rows.Count -ne 24480) { throw "Expected 24480 measurements; got $($rows.Count)." }
$groups = $rows | Group-Object algorithm, implementation, optimization, message_bytes, ad_bytes
$summary = foreach ($group in $groups) {
    $sample = $group.Group[0]
    $byRun = @($group.Group | Group-Object run)
    if ($byRun.Count -ne 3 -or @($byRun | Where-Object { $_.Count -ne 30 }).Count -ne 0) {
        throw "Expected three runs of 30 batches for $($group.Name)."
    }
    $values = @($group.Group | ForEach-Object {
        if ([double]$_.frequency -le 0 -or [double]$_.iterations -le 0 -or [double]$_.ticks -le 0) {
            throw "Invalid timing row in $($group.Name)."
        }
        [double]$_.ticks * 1e9 / [double]$_.frequency / [double]$_.iterations
    })
    $overheads = @($group.Group | ForEach-Object {
        [double]$_.timer_overhead_ticks * 1e9 / [double]$_.frequency
    })
    $runMedians = @($byRun | ForEach-Object {
        $v = @($_.Group | ForEach-Object {
            [double]$_.ticks * 1e9 / [double]$_.frequency / [double]$_.iterations
        })
        Get-Quantile $v 0.5
    })
    $median = Get-Quantile $values 0.5
    $mlen = [int]$sample.message_bytes
    $perByte = ''
    $throughput = ''
    if ($mlen -gt 0) {
        $perByte = $median / $mlen
        $throughput = $mlen * 1e9 / $median / 1048576
    }
    [pscustomobject][ordered]@{
        algorithm = $sample.algorithm
        implementation = $sample.implementation
        optimization = $sample.optimization
        message_bytes = $mlen
        ad_bytes = [int]$sample.ad_bytes
        runs = $byRun.Count
        batches = $group.Count
        median_ns_per_op = [Math]::Round($median, 3)
        q1_ns_per_op = [Math]::Round((Get-Quantile $values 0.25), 3)
        q3_ns_per_op = [Math]::Round((Get-Quantile $values 0.75), 3)
        min_run_median_ns = [Math]::Round((Get-Quantile $runMedians 0), 3)
        max_run_median_ns = [Math]::Round((Get-Quantile $runMedians 1), 3)
        median_timer_overhead_ns = [Math]::Round((Get-Quantile $overheads 0.5), 3)
        median_ns_per_plaintext_byte = $perByte
        plaintext_mib_per_second = $throughput
    }
}
if (@($summary).Count -ne 272) { throw "Expected 272 configurations; got $(@($summary).Count)." }
$summary | Sort-Object optimization, algorithm, implementation, message_bytes, ad_bytes |
    Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8
Write-Host "Wrote $(@($summary).Count) configuration summaries to $OutputPath"
