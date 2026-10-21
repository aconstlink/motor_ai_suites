param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int[]]$Geometries = @(1,16,256,4096),
    [ValidateSet('linear','direct','paired')][string[]]$Modes = @('linear','direct'),
    [ValidateRange(1,10)][int]$Repeats = 3,
    [ValidateRange(10,10000)][int]$Frames = 400,
    [ValidateRange(2,10000)][int]$Warmup = 80
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$rows = [System.Collections.Generic.List[object]]::new()
$oldMode = $env:MOTOR_VAO_LOOKUP
try {
    foreach($repeat in 1..$Repeats) {
        $sizes = @($Geometries)
        if($repeat % 2 -eq 0) { [array]::Reverse($sizes) }
        foreach($size in $sizes) {
            $order = @($Modes)
            if($repeat % 2 -eq 0) { [array]::Reverse($order) }
            foreach($mode in $order) {
                $env:MOTOR_VAO_LOOKUP = $mode
                $name = "$size-$mode-r$repeat"
                $stdout = Join-Path $output "$name.stdout.txt"
                $stderr = Join-Path $output "$name.stderr.txt"
                Write-Output "Running $name"
                $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
                    -ArgumentList @('--geometries',$size,'--draws',4096,'--warmup',$Warmup,'--frames',$Frames) `
                    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
                try {
                    if(!$p.WaitForExit(120000)) { throw "Timeout: $name" }
                    $p.WaitForExit(); $p.Refresh()
                    if($p.ExitCode -ne 0) { throw "Failed $name, exit $($p.ExitCode)" }
                    $text = Get-Content -LiteralPath $stdout -Raw
                    if($text -notmatch "VAO_LOOKUP_PROBE $mode") { throw "Diagnostic patch/mode not confirmed: $name" }
                    if($text -notmatch 'Memory manager has no entries') { throw "Missing empty memory dump: $name" }
                    $errors = @($text -split "`r?`n" | Where-Object {
                        ($_ -match '\[e\]|\[FAIL\]|invalid id') -and $_ -notmatch '\[WGL Context\] : wglMakeCurrent\(00\)'
                    })
                    if($errors.Count -ne 0) { throw "Unexpected engine error in $name : $($errors -join '; ')" }
                    $match = [regex]::Match($text,'VAO_BENCH ([^\r\n]+)')
                    if(!$match.Success) { throw "Missing result: $name" }
                    $row = [ordered]@{repeat=$repeat; mode=$mode}
                    foreach($pair in [regex]::Matches($match.Groups[1].Value,'(\w+)=([^ ]+)')) {
                        $row[$pair.Groups[1].Value] = $pair.Groups[2].Value
                    }
                    $paired = [regex]::Match($text,'VAO_PAIR_SAMPLES ([^\r\n]+)')
                    if($paired.Success) {
                        foreach($pair in [regex]::Matches($paired.Groups[1].Value,'(\w+)=([^ ]+)')) {
                            $row[$pair.Groups[1].Value] = $pair.Groups[2].Value
                        }
                    }
                    if($mode -eq 'paired' -and (!$paired.Success -or $Warmup % 2 -ne 0 -or $Frames % 2 -ne 0)) {
                        throw "Paired mode requires pair samples and even warmup/frames: $name"
                    }
                    if([int]$row.geometries -ne $size -or [int]$row.draws -ne 4096 -or [int]$row.frames -ne $Frames) {
                        throw "Unexpected workload: $name"
                    }
                    $rows.Add([pscustomobject]$row)
                    $rows | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $output 'summary.csv')
                    Write-Output $match.Value
                }
                finally {
                    if(!$p.HasExited) { $p.Kill(); $p.WaitForExit() }
                    $p.Dispose()
                }
            }
        }
    }
}
finally { $env:MOTOR_VAO_LOOKUP = $oldMode }
