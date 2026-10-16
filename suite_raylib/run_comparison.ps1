param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [string]$OutputDirectory = '',
    [string]$MotorFinishOnDirectory = '',
    [ValidateRange(1,5000)][int[]]$Objects = @(100,1000,5000),
    [ValidateRange(2,10000)][int]$Warmup = 180,
    [ValidateRange(10,10000)][int]$Frames = 600,
    [ValidateRange(1,20)][int]$Repeats = 3
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$repo = Split-Path $PSScriptRoot
$bin = Join-Path $build 'bin/Release'
$motor = (Resolve-Path -LiteralPath (Join-Path $bin '05_scene_benchmark.exe')).Path
$raylib = (Resolve-Path -LiteralPath (Join-Path $bin '00_raylib_benchmark.exe')).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $build 'raylib-comparison' }
$folder = Join-Path $OutputDirectory (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$folder = (Resolve-Path -LiteralPath $folder).Path
$cases = @(
    @{ name = 'motor-gl'; exe = $motor; args = @('--backend','gl','--path','direct','--lights','0') },
    @{ name = 'raylib-combined'; exe = $raylib; args = @('--matrices','combined') },
    @{ name = 'raylib-separate'; exe = $raylib; args = @('--matrices','separate') }
)
$runtimeFiles = @((Join-Path $bin 'motor_platform.dll'))
$sourceFiles = @((Join-Path $repo 'motor/platform/graphics/gl/gl4.cpp'))
if ($MotorFinishOnDirectory) {
    $baseline = (Resolve-Path -LiteralPath $MotorFinishOnDirectory).Path
    $baselineExe = (Resolve-Path -LiteralPath (Join-Path $baseline '05_scene_benchmark.exe')).Path
    $cases += @{ name = 'motor-gl-finish-on'; exe = $baselineExe; args = @('--backend','gl','--path','direct','--lights','0') }
    $runtimeFiles += Join-Path $baseline 'motor_platform.dll'
    $sourceFiles += Join-Path $baseline 'gl4-finish-on.cpp'
}
$metadata = [ordered]@{
    timestamp = (Get-Date).ToString('o')
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    os = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber)
    suites_commit = (git -C $repo rev-parse HEAD)
    motor_commit = (git -C (Join-Path $repo 'motor') rev-parse HEAD)
    working_tree = @(git -C $repo status --short)
    motor_working_tree = @(git -C (Join-Path $repo 'motor') status --short)
    executables = @(Get-FileHash -LiteralPath @($cases.exe | Select-Object -Unique) -Algorithm SHA256)
    motor_platform_libraries = @(Get-FileHash -LiteralPath $runtimeFiles -Algorithm SHA256)
    gl4_sources = @(Get-FileHash -LiteralPath $sourceFiles -Algorithm SHA256)
    finish_on_directory = $MotorFinishOnDirectory
    sources = @(Get-FileHash -LiteralPath (Join-Path $repo 'suite_graphics/05_scene_benchmark.cpp'),
        (Join-Path $PSScriptRoot '00_raylib_benchmark.cpp') -Algorithm SHA256)
    configuration = 'Release'; warmup = $Warmup; frames = $Frames; repeats = $Repeats
    normalize_client_size = $true; expected_client_width = 1280; expected_client_height = 720
    objects = $Objects; metric = 'CPU frame-start interval; not GPU time or display FPS'
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $folder 'environment.json')
$rows = [System.Collections.Generic.List[object]]::new()
$windowDpi = $null
if (!('BenchmarkWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BenchmarkWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
}
'@
}
$culture = [Globalization.CultureInfo]::InvariantCulture
function Number([string]$value) { return [double]::Parse($value, $culture) }
function Median($values) {
    $sorted = @($values | Sort-Object)
    return ($sorted[[int][Math]::Floor(($sorted.Count-1)/2)] + $sorted[[int][Math]::Floor($sorted.Count/2)]) / 2
}
foreach ($repeat in 1..$Repeats) {
    foreach ($count in $Objects) {
        # Rotate the starting case to reduce systematic order/thermal bias.
        foreach ($offset in 0..($cases.Count-1)) {
            $case = $cases[($repeat-1+$offset) % $cases.Count]
            $name = "$($case.name)-$count-r$repeat"
            $out = Join-Path $folder "$name.stdout.txt"
            $err = Join-Path $folder "$name.stderr.txt"
            $arguments = $case.args + @('--objects',$count,'--warmup',$Warmup,'--frames',$Frames)
            Write-Host "Running $name"
            $p = Start-Process -FilePath $case.exe -WorkingDirectory (Split-Path $case.exe) -WindowStyle Hidden `
                -ArgumentList $arguments -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
            try {
                $deadline = [DateTime]::UtcNow.AddSeconds(20)
                do {
                    $p.Refresh()
                    $window = $p.MainWindowHandle
                    if ($window -ne [IntPtr]::Zero -or $p.HasExited) { break }
                    Start-Sleep -Milliseconds 10
                } while ([DateTime]::UtcNow -lt $deadline)
                $rect = [BenchmarkWindow+Rect]::new()
                if ($window -eq [IntPtr]::Zero -or ![BenchmarkWindow]::GetClientRect($window,[ref]$rect)) {
                    throw "No readable benchmark window: $name"
                }
                if (($rect.right - $rect.left) -ne 1280 -or ($rect.bottom - $rect.top) -ne 720) {
                    $outer = [BenchmarkWindow+Rect]::new()
                    if (![BenchmarkWindow]::GetWindowRect($window,[ref]$outer)) { throw "Cannot read outer size: $name" }
                    $width = $outer.right - $outer.left + 1280 - ($rect.right - $rect.left)
                    $height = $outer.bottom - $outer.top + 720 - ($rect.bottom - $rect.top)
                    # Normalize client dimensions at startup; measured runs have a warmup period.
                    if (![BenchmarkWindow]::SetWindowPos($window,[IntPtr]::Zero,0,0,$width,$height,0x16)) {
                        throw "Cannot normalize client size: $name"
                    }
                }
                if (![BenchmarkWindow]::GetClientRect($window,[ref]$rect) -or
                    ($rect.right - $rect.left) -ne 1280 -or ($rect.bottom - $rect.top) -ne 720) {
                    throw "Expected a 1280x720 client area: $name; observed $($rect.right - $rect.left)x$($rect.bottom - $rect.top)"
                }
                $dpi = [BenchmarkWindow]::GetDpiForWindow($window)
                if ($dpi -eq 0) { throw "Cannot read window DPI: $name" }
                if ($null -eq $windowDpi) { $windowDpi = $dpi }
                elseif ($dpi -ne $windowDpi) { throw "Window DPI mismatch: $name ($dpi vs $windowDpi)" }
                if (!$p.WaitForExit(330000)) { throw "Timeout: $name" }
                $p.WaitForExit()
                if ($p.ExitCode -ne 0) { throw "Failed $name (exit $($p.ExitCode)); see $out and $err" }
                $log = Get-Content -LiteralPath $out -Raw
                if ($case.name.StartsWith('motor-gl')) {
                    $matches = [regex]::Matches($log, '(?m)(?<![A-Z_])BENCH,([^\r\n]+)')
                    if ($matches.Count -ne 1) { throw "Expected one motor result: $name" }
                    $v = $matches[0].Groups[1].Value.Split(',')
                    if ($v.Count -ne 17 -or $v[0] -ne 'gl' -or [int]$v[1] -ne $count -or
                        [int]$v[2] -ne 0 -or [int]$v[3] -ne $Frames -or [int]$v[4] -ne $count -or
                        [int]$v[5] -ne $count) { throw "Unexpected motor workload: $name" }
                    $frame = Number $v[6]; $p95 = Number $v[7]
                    $update = Number $v[10]; $submit = Number $v[12]
                } else {
                    $matches = [regex]::Matches($log, 'RAYLIB_BENCH,([^\r\n]+)')
                    if ($matches.Count -ne 1) { throw "Expected one raylib result: $name" }
                    $v = $matches[0].Groups[1].Value.Split(',')
                    if ($v.Count -ne 9 -or $v[0] -ne 'gl' -or [int]$v[1] -ne $count -or
                        [int]$v[2] -ne $Frames -or [int]$v[3] -ne $count -or
                        $v[8] -ne $case.args[1]) { throw "Unexpected raylib workload: $name" }
                    $frame = Number $v[4]; $p95 = Number $v[5]
                    $update = Number $v[6]; $submit = Number $v[7]
                }
                if ($frame -le 0 -or $p95 -lt $frame) { throw "Invalid timings: $name" }
                $rows.Add([pscustomobject][ordered]@{
                    variant = $case.name; objects = $count; repeat = $repeat; samples = $Frames
                    client_width = 1280; client_height = 720; window_dpi = $dpi
                    frame_median_ms = $frame; frame_p95_ms = $p95
                    update_median_ms = $update; submit_median_ms = $submit
                })
                $rows | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $folder 'results.csv')
                Write-Host ("  median {0:F3} ms; p95 {1:F3} ms" -f $frame,$p95)
            } finally {
                if (!$p.HasExited) { $p.Kill(); $p.WaitForExit() }
                $p.Dispose()
            }
            Start-Sleep -Seconds 1
        }
    }
}
$summary = foreach ($count in $Objects) {
    foreach ($case in $cases) {
        $group = @($rows | Where-Object { $_.objects -eq $count -and $_.variant -eq $case.name })
        [pscustomobject][ordered]@{
            variant = $case.name; objects = $count; runs = $group.Count
            median_of_frame_medians_ms = Median $group.frame_median_ms
            min_frame_median_ms = ($group.frame_median_ms | Measure-Object -Minimum).Minimum
            max_frame_median_ms = ($group.frame_median_ms | Measure-Object -Maximum).Maximum
            median_of_frame_p95_ms = Median $group.frame_p95_ms
            median_of_update_medians_ms = Median $group.update_median_ms
            median_of_submit_medians_ms = Median $group.submit_median_ms
        }
    }
}
$summary | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $folder 'summary.csv')
$summary | Format-Table variant,objects,median_of_frame_medians_ms,min_frame_median_ms,max_frame_median_ms
Write-Host "Results: $folder"
