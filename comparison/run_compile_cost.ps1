param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\docs\results\compile-cost-local'),
    [uint32]$SeedBase = 2026102400,
    [ValidateRange(1,10)][int]$Repetitions = 3
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $BuildDirectory 'bin\Release'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
function Run-Test([string]$name, [string]$exe, [string[]]$arguments) {
    $process = Start-Process -FilePath (Join-Path $bin $exe) -ArgumentList $arguments -WorkingDirectory $bin -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $out "$name.log") -RedirectStandardError (Join-Path $out "$name.err")
    $handle = $process.Handle
    if (-not $process.WaitForExit(90000)) {
        Stop-Process -Id $process.Id -Force
        throw "$name timed out"
    }
    $process.Refresh()
    Write-Output "$name exit=$($process.ExitCode)"
    if ($process.ExitCode -ne 0) { throw "$name failed" }
}
Run-Test 'render-access' '08_render_access_cost.exe' @('--benchmark')
Run-Test 'variable-set' '06_variable_set_cost.exe' @('--benchmark')
for ($r = 0; $r -lt $Repetitions; ++$r) {
    $modes = if ($r % 2 -eq 0) { @('motor', 'sync', 'worker') } else { @('worker', 'sync', 'motor') }
    foreach ($mode in $modes) {
        $offset = switch ($mode) { 'motor' { 0 } 'sync' { 1 } 'worker' { 2 } }
        $seed = [string]($SeedBase + 10 * $r + $offset)
        if ($mode -eq 'motor') { Run-Test "motor-$r" '13_live_recompile.exe' @('--benchmark', '--gl-only', '--seed', $seed) }
        else { Run-Test "$mode-$r" '01_raylib_compile_cost.exe' @("--$mode", '--seed', $seed) }
    }
}
