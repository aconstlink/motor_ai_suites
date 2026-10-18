param(
    [string]$Executable = "$PSScriptRoot/../../build/bin/Release/15_village.exe",
    [string]$OutputDirectory = "$PSScriptRoot/../../build/village-backend-traces",
    [ValidateRange(10,10000)][int]$Warmup = 120,
    [ValidateRange(10,10000)][int]$Frames = 360,
    [ValidateRange(1,10)][int]$Repeats = 3
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$folder = Join-Path $OutputDirectory (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$folder = (Resolve-Path -LiteralPath $folder).Path
function Number($value) { [double]::Parse($value,[cultureinfo]::InvariantCulture) }
$results = [System.Collections.Generic.List[object]]::new()
$environment = [ordered]@{
    timestamp = (Get-Date).ToString('o'); executable = $exe
    cpu = (Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    gpu = (Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    warmup = $Warmup; frames = $Frames; repeats = $Repeats
    note = 'Requires temporary village_backend_trace.patch. Single D3D11 window, no optimizations, original sleeps unchanged, no GPU timestamp queries.'
}
$environment | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $folder 'environment.json')
$cases = @(@{tiles=1;detail='high'},@{tiles=4;detail='high'},@{tiles=4;detail='low'})
$old = $env:MOTOR_VILLAGE_BACKEND_TRACE
try {
    foreach($repeat in 1..$Repeats) {
        $order = @(0,1,2)
        if($repeat % 2 -eq 0) { [array]::Reverse($order) }
        foreach($index in $order) {
            $case = $cases[$index]
            $name = "$($case.tiles)-$($case.detail)-r$repeat"
            Write-Host "Tracing $name"
            $csv = Join-Path $folder "$name.trace.csv"
            $out = Join-Path $folder "$name.stdout.txt"
            $err = Join-Path $folder "$name.stderr.txt"
            $env:MOTOR_VILLAGE_BACKEND_TRACE = $csv
            $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
                -ArgumentList @('--d3d-only','--benchmark','--tiles',$case.tiles,'--detail',$case.detail,
                    '--warmup',$Warmup,'--frames',$Frames,'--total-particles',32768) `
                -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
            try {
                if(-not $p.WaitForExit(210000)) { throw "Timeout: $name" }
                $p.WaitForExit(); $p.Refresh()
                if($p.ExitCode -ne 0) { throw "Failed $name ($($p.ExitCode)); see logs" }
            }
            finally { if(-not $p.HasExited){$p.Kill();$p.WaitForExit()}; $p.Dispose() }
            $text = Get-Content -LiteralPath $out -Raw
            $text = [regex]::Replace($text,'\x1B\[[0-?]*[ -/]*[@-~]','')
            if($text -notmatch '\[OK\] \[village diagnostics\] backend trace written without dropped records') {
                throw "Missing/failed instrumentation: apply the diagnostic patch and rebuild ($name)"
            }
            $window = [regex]::Match($text,'VILLAGE_BENCH_WINDOW start_ns=(\d+) end_ns=(\d+)')
            $app = [regex]::Match($text,'VILLAGE_BENCH ([^\r\n]+)')
            if(-not $window.Success -or -not $app.Success) { throw "Missing benchmark window: $name" }
            $lo = [long]$window.Groups[1].Value; $hi = [long]$window.Groups[2].Value
            $rows = @(Import-Csv -LiteralPath $csv | Where-Object { [long]$_.previous_end_ns -ge $lo -and [long]$_.end_ns -le $hi })
            if($rows.Count -lt $Frames-5 -or $rows.Count -gt $Frames+2) { throw "Unexpected aligned frame count: $($rows.Count) ($name)" }
            $result = [ordered]@{run=$name; trace_frames=$rows.Count; window_start_ns=$lo; window_end_ns=$hi}
            foreach($pair in [regex]::Matches($app.Groups[1].Value,'(\w+)=([^ ]+)')) {
                $result[$pair.Groups[1].Value] = $pair.Groups[2].Value
            }
            foreach($key in @('gap_ms','setup_ms','begin_ms','execute_ms','end_ms','present_ms','deactivate_ms',
                'miss_sleep_ms','outer_sleep_ms','maintenance_ms','miss_sleep_count','outer_sleep_count','empty_polls')) {
                $sum = 0.0; foreach($row in $rows){$sum += Number $row.$key}
                $result[$key] = $sum/$rows.Count
            }
            $result['active_ms'] = $result.setup_ms+$result.begin_ms+$result.execute_ms+$result.end_ms+$result.present_ms+$result.deactivate_ms
            $result['cycle_ms'] = $result.active_ms+$result.gap_ms
            $result['gap_other_ms'] = $result.gap_ms-$result.miss_sleep_ms-$result.outer_sleep_ms-$result.maintenance_ms
            $result['sleep_call_mean_ms'] = if($result.miss_sleep_count -gt 0){$result.miss_sleep_ms/$result.miss_sleep_count}else{0}
            $result['sleep_frame_fraction'] = @($rows | Where-Object {[int]$_.miss_sleep_count -gt 0}).Count/$rows.Count
            $results.Add([pscustomobject]$result)
            $results | ForEach-Object {
                $export = [ordered]@{}
                foreach($property in $_.PSObject.Properties) {
                    $export[$property.Name] = if($property.Value -is [double]) {
                        $property.Value.ToString('R',[cultureinfo]::InvariantCulture)
                    } else { $property.Value }
                }
                [pscustomobject]$export
            } | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $folder 'summary.csv')
            Write-Host ("{0}: frame {1} ms, execute {2:F3}, present {3:F3}, backoff sleep {4:F3}, gap {5:F3}" -f `
                $name,$result.mean_ms,$result.execute_ms,$result.present_ms,$result.miss_sleep_ms,$result.gap_ms)
        }
    }
}
finally { $env:MOTOR_VILLAGE_BACKEND_TRACE = $old }
Write-Host "Results: $folder"
