param(
    [string]$Executable = "$PSScriptRoot/../build/bin/Release/15_village.exe",
    [string]$OutputDirectory = "$PSScriptRoot/../build/village-benchmark-results",
    [ValidateSet('gl4','d3d11')][string[]]$Backends = @('gl4','d3d11'),
    [ValidateRange(10,10000)][int]$Warmup = 180,
    [ValidateRange(10,10000)][int]$Frames = 600,
    [ValidateRange(1,20)][int]$Repeats = 3,
    [ValidateRange(1024,262144)][int]$TotalParticles = 32768
)
$ErrorActionPreference = 'Stop'
if ($TotalParticles % 4 -ne 0) { throw 'TotalParticles must be divisible by four' }
$exe = (Resolve-Path -LiteralPath $Executable).Path
$folder = Join-Path $OutputDirectory (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$folder = (Resolve-Path -LiteralPath $folder).Path
$results = [System.Collections.Generic.List[object]]::new()
$environment = [ordered]@{
    timestamp = (Get-Date).ToString('o'); executable = $exe
    cpu = (Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    gpu = (Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    warmup = $Warmup; frames = $Frames; repeats = $Repeats; total_particles = $TotalParticles
    notes = 'Single-window runs, requested vsync off, fixed camera and animation, HDR/shadows/particles on. Frame cadence and CPU callback wall times; no GPU timestamps.'
}
$environment | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $folder 'environment.json')
$cases = @(
    @{tiles=1; detail='high'}, @{tiles=4; detail='high'},
    @{tiles=1; detail='low'}, @{tiles=4; detail='low'}
)
foreach ($repeat in 1..$Repeats) {
    $order = @(0,1,2,3)
    if ($repeat % 2 -eq 0) { [array]::Reverse($order) }
    foreach ($backend in $Backends) {
        foreach ($index in $order) {
            $case = $cases[$index]
            $name = "$backend-$($case.tiles)-$($case.detail)-r$repeat"
            Write-Host "Running $name"
            $out = Join-Path $folder "$name.stdout.txt"
            $err = Join-Path $folder "$name.stderr.txt"
            $backendFlag = if ($backend -eq 'gl4') { '--gl-only' } else { '--d3d-only' }
            $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
                -ArgumentList @($backendFlag,'--benchmark','--tiles',$case.tiles,'--detail',$case.detail,
                    '--warmup',$Warmup,'--frames',$Frames,'--total-particles',$TotalParticles) `
                -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
            try {
                if (-not $p.WaitForExit(210000)) { throw "Timeout: $name" }
                $p.WaitForExit(); $p.Refresh()
                if ($p.ExitCode -ne 0) { throw "Failed $name (exit $($p.ExitCode)); see $out and $err" }
                $text = Get-Content -LiteralPath $out -Raw
                $match = [regex]::Match($text,'VILLAGE_BENCH ([^\r\n]+)')
                if (-not $match.Success) { throw "No benchmark result: $name" }
                $row = [ordered]@{repeat=$repeat}
                foreach ($pair in [regex]::Matches($match.Groups[1].Value,'(\w+)=([^ ]+)')) {
                    $row[$pair.Groups[1].Value] = $pair.Groups[2].Value
                }
                if ($row.backend -ne $backend -or [int]$row.tiles -ne $case.tiles -or $row.detail -ne $case.detail -or
                    [int]$row.frames -ne $Frames -or [int]$row.vsync -ne 0 -or [int]$row.particles -ne $TotalParticles -or
                    [int]$row.width -ne 1280 -or [int]$row.height -ne 800) { throw "Unexpected configuration: $name" }
                $results.Add([pscustomobject]$row)
                $results | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $folder 'results.csv')
                Write-Host $match.Value
            }
            finally {
                if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
                $p.Dispose()
            }
        }
    }
}
Write-Host "Results: $folder"
