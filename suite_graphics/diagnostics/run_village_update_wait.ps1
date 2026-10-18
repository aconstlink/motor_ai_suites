param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$OutputDirectory = "$PSScriptRoot/results",
    [int]$Frames = 900,
    [int]$Repeats = 3,
    [string[]]$Modes = @('d3d11','gl4','dual'),
    [string]$Label = 'update-wait'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
$views = [System.Collections.Generic.List[object]]::new()
$oldFrames = $env:MOTOR_VILLAGE_FRAME_CSV
$oldTrace = $env:MOTOR_VILLAGE_BACKEND_TRACE
Remove-Item Env:MOTOR_VILLAGE_BACKEND_TRACE -ErrorAction SilentlyContinue
try {
    for($repeat=1; $repeat -le $Repeats; ++$repeat) {
        for($j=0; $j -lt $Modes.Count; ++$j) {
            $mode = $Modes[($j+$repeat-1)%$Modes.Count]
            $flag = switch($mode){ 'd3d11' {'--d3d-only'} 'gl4' {'--gl-only'} 'dual' {'--dual'} default {throw 'Unsupported backend mode'} }
            $name = "$Label-$mode-r$repeat"
            $out = Join-Path $OutputDirectory "$name.stdout.txt"
            $err = Join-Path $OutputDirectory "$name.stderr.txt"
            $csv = Join-Path $OutputDirectory "$name.frames.csv"
            $env:MOTOR_VILLAGE_FRAME_CSV = [IO.Path]::GetFullPath($csv)
            $p = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden `
                -ArgumentList @('--benchmark',$flag,'--tiles','4','--detail','high','--warmup','120',
                    '--frames',$Frames,'--total-particles','32768','--cpu-sleep-us','0') `
                -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
            try {
                if(-not $p.WaitForExit(210000)){throw "Timeout: $name"}
                $p.WaitForExit(); $p.Refresh()
                if($p.ExitCode -ne 0){throw "Exit $($p.ExitCode): $name"}
            } finally {
                if(-not $p.HasExited){$p.Kill(); $p.WaitForExit()}
                $p.Dispose()
            }
            $text = [regex]::Replace((Get-Content -LiteralPath $out -Raw),'\x1B\[[0-?]*[ -/]*[@-~]','')
            if($text -match '\[FAIL\]' -or -not $text.Contains('Memory manager has no entries.')){throw "Validation failed: $name"}
            $result = [ordered]@{run=$name; mode=$mode; repeat=$repeat}
            foreach($prefix in @('VILLAGE_SLEEP','VILLAGE_BENCH','VILLAGE_CPU')) {
                $line = [regex]::Match($text,"$prefix ([^\r\n]+)")
                if(-not $line.Success){throw "Missing $prefix : $name"}
                foreach($pair in [regex]::Matches($line.Groups[1].Value,'(\w+)=([^ ]+)')){
                    $result[$pair.Groups[1].Value] = $pair.Groups[2].Value
                }
            }
            if([int]$result.cpu_sleep_us -ne 0){throw "Sleep unexpectedly enabled: $name"}
            $samples = @(Import-Csv $csv | ForEach-Object {[double]::Parse($_.frame_ms,[cultureinfo]::InvariantCulture)})
            if($samples.Count -ne $Frames){throw "Wrong sample count: $name"}
            $sorted = @($samples | Sort-Object)
            $result['p99_ms'] = $sorted[[math]::Ceiling(.99*$Frames)-1].ToString('F4',[cultureinfo]::InvariantCulture)
            $result['max_ms'] = $sorted[-1].ToString('F4',[cultureinfo]::InvariantCulture)
            $result['over_8_33ms'] = @($samples | Where-Object {$_ -gt (1000.0/120)}).Count
            $result['over_16_67ms'] = @($samples | Where-Object {$_ -gt (1000.0/60)}).Count
            $result['over_33_33ms'] = @($samples | Where-Object {$_ -gt (1000.0/30)}).Count
            $viewLines = [regex]::Matches($text,'VILLAGE_VIEW ([^\r\n]+)')
            $expectedViews = if($mode -eq 'dual'){2}else{1}
            if($viewLines.Count -ne $expectedViews){throw "Wrong view count: $name"}
            foreach($line in $viewLines) {
                $view=[ordered]@{run=$name}
                foreach($pair in [regex]::Matches($line.Groups[1].Value,'(\w+)=([^ ]+)')){$view[$pair.Groups[1].Value]=$pair.Groups[2].Value}
                if([int]$view.frames -ne $Frames -or [int]$view.draws -ne 347){throw "Incomplete view: $name"}
                $views.Add([pscustomobject]$view)
            }
            $results.Add([pscustomobject]$result)
            $results | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory "$Label.csv")
            $views | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory "$Label.views.csv")
            Write-Output "$name mean=$($result.mean_ms) ms; p95=$($result.p95_ms); p99=$($result.p99_ms); max=$($result.max_ms); FPS=$($result.fps); CPU=$($result.machine_percent)%"
        }
    }
} finally { $env:MOTOR_VILLAGE_FRAME_CSV=$oldFrames; $env:MOTOR_VILLAGE_BACKEND_TRACE=$oldTrace }
