param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$Repeats = 3,
    [int]$Frames = 800
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path $Executable).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$oldMode = $env:MOTOR_VILLAGE_VAO
$oldCsv = $env:MOTOR_VILLAGE_FRAME_CSV
$rows = [System.Collections.Generic.List[object]]::new()
$culture = [Globalization.CultureInfo]::InvariantCulture
function Number($text) { return [double]::Parse($text,$culture) }
function Mean($values) { return ($values | Measure-Object -Average).Average }
function Fields($text) {
    $data = [ordered]@{}
    foreach($m in [regex]::Matches($text,'(\w+)=([^ ]+)')) { $data[$m.Groups[1].Value] = $m.Groups[2].Value }
    return [pscustomobject]$data
}
try {
    $env:MOTOR_VILLAGE_VAO = 'paired'
    foreach($repeat in 1..$Repeats) {
        $tiles = if($repeat % 2 -eq 0) { @(4,1) } else { @(1,4) }
        foreach($tile in $tiles) {
            $name = "tiles$tile-r$repeat"
            $stdout = Join-Path $output "$name.stdout.txt"
            $stderr = Join-Path $output "$name.stderr.txt"
            $env:MOTOR_VILLAGE_FRAME_CSV = Join-Path $output "$name.frames.csv"
            Write-Output "Running $name"
            $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
                -ArgumentList @('--gl-only','--benchmark','--detail','high','--tiles',$tile,'--warmup',180,'--frames',$Frames,'--total-particles',32768) `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
            try {
                if(!$p.WaitForExit(180000)) { throw "Timeout: $name" }
                $p.WaitForExit(); $p.Refresh()
                $text = Get-Content $stdout -Raw
                if($p.ExitCode -ne 0 -or $text -notmatch 'Memory manager has no entries' -or $text -match '\[FAIL\]') { throw "Failed: $name" }
                if($text -notmatch 'VILLAGE_VAO_PROBE mode=paired') { throw "Missing diagnostic patch: $name" }
                $benchmark = [regex]::Match($text,'VILLAGE_BENCH ([^\r\n]+)')
                if(!$benchmark.Success) { throw "No benchmark result: $name" }
                $b = Fields $benchmark.Groups[1].Value
                $samples = @([regex]::Matches($text,'VILLAGE_VAO_SAMPLE ([^\r\n]+)') | ForEach-Object { Fields $_.Groups[1].Value })
                if($samples.Count -lt $Frames-2) { throw "Too few backend samples: $name" }
                if(@($samples | Where-Object { [int]$_.calls -ne [int]$b.draws }).Count) { throw "Unexpected draw counts: $name" }
                $samples | Export-Csv -NoTypeInformation (Join-Path $output "$name.backend.csv")
                $linear = @($samples | Where-Object direct -eq '0')
                $direct = @($samples | Where-Object direct -eq '1')
                $deltas = @(
                    for($i=0; $i+1 -lt $samples.Count; $i+=2) {
                        $a = $samples[$i]; $c = $samples[$i+1]
                        if($a.direct -eq $c.direct) { throw "Modes did not alternate: $name" }
                        $sign = if($a.direct -eq '0') { 1 } else { -1 }
                        $sign * ((Number $a.backend_ms) - (Number $c.backend_ms))
                    }
                )
                $sorted = @($deltas | Sort-Object)
                $rows.Add([pscustomobject][ordered]@{
                    repeat=$repeat; tiles=$tile; draws=[int]$b.draws; links=($samples | Measure-Object max_links -Maximum).Maximum
                    samples=$samples.Count; frame_mean_ms=(Number $b.mean_ms)
                    linear_backend_ms=(Mean @($linear | ForEach-Object { Number $_.backend_ms }))
                    direct_backend_ms=(Mean @($direct | ForEach-Object { Number $_.backend_ms }))
                    pair_delta_mean_ms=(Mean $deltas); pair_delta_median_ms=$sorted[[int][math]::Floor($sorted.Count/2)]
                    direct_hit_percent=100.0*($direct | Measure-Object hits -Sum).Sum/($direct | Measure-Object calls -Sum).Sum
                })
                $rows | Export-Csv -NoTypeInformation (Join-Path $output 'summary.csv')
                Write-Output ($rows[$rows.Count-1] | ConvertTo-Json -Compress)
            } finally {
                if(!$p.HasExited) { $p.Kill(); $p.WaitForExit() }
                $p.Dispose()
            }
        }
    }
} finally { $env:MOTOR_VILLAGE_VAO=$oldMode; $env:MOTOR_VILLAGE_FRAME_CSV=$oldCsv }
