param(
    [Parameter(Mandatory=$true)][string]$Shaderc,
    [Parameter(Mandatory=$true)][string]$BgfxSource
)
$ErrorActionPreference = 'Stop'
foreach ($backend in @('gl','d3d')) {
    $profile = if ($backend -eq 'gl') { '330' } else { 's_5_0' }
    $destination = Join-Path $PSScriptRoot "bin/$backend"
    New-Item -ItemType Directory -Force $destination | Out-Null
    foreach ($stage in @('vertex','fragment')) {
        & $Shaderc -f (Join-Path $PSScriptRoot "$stage.sc") -o (Join-Path $destination "$stage.bin") `
            --type $stage --platform windows -p $profile `
            --varyingdef (Join-Path $PSScriptRoot 'varying.def.sc') -i (Join-Path $BgfxSource 'src')
        if ($LASTEXITCODE -ne 0) { throw "shaderc failed: $backend/$stage" }
    }
}
