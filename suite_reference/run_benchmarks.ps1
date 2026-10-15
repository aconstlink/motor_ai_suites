param(
    [string]$BuildDirectory = './build',
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'Release',
    [ValidateRange(1,5000)][int[]]$Objects = @(1000),
    [ValidateRange(1,20)][int]$Repeats = 3,
    [ValidateRange(2,10000)][int]$Warmup = 120,
    [ValidateRange(10,10000)][int]$Frames = 300
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path $BuildDirectory).Path
$bin = Join-Path $build "bin/$Configuration"
if (!(Test-Path $bin)) { $bin = Join-Path $build 'bin' }
$cases = @(
    @{ Name='opengl-gl'; Exe='00_opengl_benchmark'; Backend='gl' },
    @{ Name='cinder-gl'; Exe='00_cinder_benchmark'; Backend='gl' },
    @{ Name='bgfx-gl'; Exe='00_bgfx_benchmark'; Backend='gl' }
)
if ($env:OS -eq 'Windows_NT') { $cases += @{ Name='bgfx-d3d'; Exe='00_bgfx_benchmark'; Backend='d3d' } }
foreach ($case in $cases) {
    $extension = if ($env:OS -eq 'Windows_NT') { '.exe' } else { '' }
    $case.Path = Join-Path $bin ($case.Exe + $extension)
    if (!(Test-Path $case.Path)) { throw "Build the requested references first: $($case.Path)" }
}
$output = Join-Path $build ('reference-results/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $output | Out-Null
$metadata = foreach ($case in $cases) {
    [pscustomobject]@{ Name=$case.Name; Path=$case.Path; SHA256=(Get-FileHash $case.Path).Hash }
}
$metadata | Export-Csv (Join-Path $output 'executables.csv') -NoTypeInformation
$rows = @()
for ($round=0; $round -lt $Repeats; ++$round) {
    foreach ($count in $Objects) {
        for ($offset=0; $offset -lt $cases.Count; ++$offset) {
            $case = $cases[($round + $offset) % $cases.Count]
            $name = "$($case.Name)-$count-r$($round+1)"
            $log = Join-Path $output "$name.log"
            $args = "--backend $($case.Backend) --objects $count --warmup $Warmup --frames $Frames"
            $start = @{ FilePath=$case.Path; ArgumentList=$args; WorkingDirectory=$bin; PassThru=$true;
                RedirectStandardOutput=$log; RedirectStandardError=(Join-Path $output "$name.err.log") }
            # These are visible graphics tests, not background helpers. Cinder pauses hidden windows.
            if ($env:OS -eq 'Windows_NT') { $start.WindowStyle = 'Normal' }
            $process = Start-Process @start
            if (!$process.WaitForExit(320000)) { $process.Kill(); $process.WaitForExit(); throw "Timeout: $name" }
            $process.WaitForExit()
            if ($process.ExitCode -ne 0) { throw "Failed: $name; see $log" }
            $record = Get-Content $log | Where-Object { $_ -match 'REFERENCE_BENCH,' } | Select-Object -Last 1
            if (!$record) { throw "Missing result: $name" }
            $csv = $record.Substring($record.IndexOf('REFERENCE_BENCH,'))
            $data = $csv | ConvertFrom-Csv -Header Tag,Engine,Backend,Objects,Samples,Draws,MedianMs,P95Ms,UpdateMs,SubmitMs
            if ([int]$data.Samples -ne $Frames -or [int]$data.Draws -ne $count) { throw "Incomplete result: $name" }
            $rows += [pscustomobject]@{ Run=$round+1; Case=$case.Name; Objects=$count; Samples=$data.Samples;
                MedianMs=$data.MedianMs; P95Ms=$data.P95Ms; UpdateMs=$data.UpdateMs; SubmitMs=$data.SubmitMs }
            $rows | Export-Csv (Join-Path $output 'runs.csv') -NoTypeInformation
            Write-Host "$name : $($data.MedianMs) ms"
        }
    }
}
Write-Host "Results: $output"
