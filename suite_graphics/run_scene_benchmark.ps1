param(
    [string]$Executable = "$PSScriptRoot/../build/bin/Release/05_scene_benchmark.exe",
    [string]$OutputDirectory = "$PSScriptRoot/../build/benchmark-results",
    [ValidateSet('gl','d3d','dual')][string[]]$Backends = @('gl','d3d'),
    [ValidateSet('scene','direct')][string]$RenderPath = 'scene',
    [ValidateSet('multipass','singlepass')][string]$Lighting = 'multipass',
    [int[]]$Objects = @(100,1000,5000),
    [int[]]$Lights = @(0,1,2,4,8),
    [ValidateRange(2,10000)][int]$Warmup = 120,
    [ValidateRange(10,10000)][int]$Frames = 300,
    [ValidateRange(1,20)][int]$Repeats = 1
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
foreach ($n in $Objects) { if ($n -lt 1 -or $n -gt 5000) { throw 'Objects must be 1..5000' } }
foreach ($n in $Lights) { if ($n -lt 0 -or $n -gt 8) { throw 'Lights must be 0..8' } }
if ($RenderPath -eq 'direct' -and @($Lights | Where-Object { $_ -ne 0 }).Count -ne 0) { throw 'Direct mode requires -Lights 0' }
if ($Lighting -eq 'singlepass' -and ($RenderPath -ne 'scene' -or @($Lights | Where-Object { $_ -notin @(2,3) }).Count -ne 0)) { throw 'Singlepass requires -RenderPath scene and -Lights 2,3 (or either count)' }
$folder = Join-Path $OutputDirectory (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$folder = (Resolve-Path -LiteralPath $folder).Path
$headers = @('backend','objects','lights','samples','expected_draws_per_frame','variable_sets',
    'frame_median_ms','frame_p95_ms','sync_median_ms','sync_p95_ms','trafo_median_ms','trafo_p95_ms',
    'prepare0_median_ms','prepare0_p95_ms','prepare1_median_ms','prepare1_p95_ms','lighting')
$results = [System.Collections.Generic.List[object]]::new()
$environment = [ordered]@{
    timestamp = (Get-Date).ToString('o'); executable = $exe
    cpu = (Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    gpu = (Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    warmup = $Warmup; frames = $Frames; render_path = $RenderPath; lighting = $Lighting
}
$environment | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $folder 'environment.json')
foreach ($repeat in 1..$Repeats) {
    foreach ($backend in $Backends) {
        foreach ($count in $Objects) {
            foreach ($light in $Lights) {
                $name = "$RenderPath-$Lighting-$backend-$count-$light-r$repeat"
                Write-Host "Running $name"
                $out = Join-Path $folder "$name.stdout.txt"
                $err = Join-Path $folder "$name.stderr.txt"
                $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
                    -ArgumentList @('--path',$RenderPath,'--lighting',$Lighting,'--backend',$backend,'--objects',$count,'--lights',$light,'--warmup',$Warmup,'--frames',$Frames) `
                    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
                try {
                    if (-not $p.WaitForExit(330000)) { throw "Timeout: $name" }
                    $p.WaitForExit()
                    if ($p.ExitCode -ne 0) { throw "Failed $name (exit $($p.ExitCode)); see $out and $err" }
                    $text = Get-Content -LiteralPath $out -Raw
                    $match = [regex]::Match($text,'BENCH,([^\r\n]+)')
                    if (-not $match.Success) { throw "No benchmark result: $name" }
                    $row = $match.Groups[1].Value | ConvertFrom-Csv -Header $headers
                    if ($row.lighting -ne $Lighting) { throw "Lighting mode missing or mismatched: $name; rebuild the benchmark" }
                    $row | Add-Member -NotePropertyName repeat -NotePropertyValue $repeat
                    $row | Add-Member -NotePropertyName render_path -NotePropertyValue $RenderPath
                    $results.Add($row)
                    $results | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $folder 'results.csv')
                }
                finally {
                    if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
                    $p.Dispose()
                }
            }
        }
    }
}
Write-Host "Results: $folder"
