param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
foreach ($model in @('Sponza','Suzanne')) {
    $original = Join-Path $Source "2.0/$model"
    $target = Join-Path $Destination $model
    New-Item -ItemType Directory -Force $target | Out-Null
    $data = Get-Content (Join-Path $original "glTF/$model.gltf") -Raw | ConvertFrom-Json
    foreach ($key in @('images','textures','samplers')) { $data.PSObject.Properties.Remove($key) }
    foreach ($mesh in $data.meshes) {
        foreach ($primitive in $mesh.primitives) {
            # D3D11 currently expects shader inputs first in the vertex layout.
            $attributes = [ordered]@{}
            foreach ($key in @('POSITION','NORMAL')) {
                if ($null -ne $primitive.attributes.PSObject.Properties[$key]) {
                    $attributes[$key] = $primitive.attributes.$key
                }
            }
            foreach ($property in $primitive.attributes.PSObject.Properties) {
                if (-not $attributes.Contains($property.Name)) { $attributes[$property.Name] = $property.Value }
            }
            $primitive.attributes = [pscustomobject]$attributes
        }
    }
    $materialIndex = 0
    foreach ($material in $data.materials) {
        # motor currently requires material names; glTF itself makes them optional.
        if ([string]::IsNullOrEmpty($material.name)) {
            $material | Add-Member -Force NoteProperty name "material_$materialIndex"
        }
        ++$materialIndex
        foreach ($key in @('normalTexture','occlusionTexture','emissiveTexture')) {
            $material.PSObject.Properties.Remove($key)
        }
        foreach ($key in @('baseColorTexture','metallicRoughnessTexture')) {
            $material.pbrMetallicRoughness.PSObject.Properties.Remove($key)
        }
    }
    $data.asset | Add-Member -Force NoteProperty copyright "Model authors and notices: see UPSTREAM_README.md; source: KhronosGroup/glTF-Sample-Models/2.0/$model. Textures omitted for the motor async-loading sample."
    [IO.File]::WriteAllText((Join-Path $target "$model.gltf"), ($data | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $original "glTF/$model.bin") -Destination $target
    Copy-Item -LiteralPath (Join-Path $original 'README.md') -Destination (Join-Path $target 'UPSTREAM_README.md')
    if ((Get-FileHash (Join-Path $original "glTF/$model.bin")).Hash -ne (Get-FileHash (Join-Path $target "$model.bin")).Hash) {
        throw "Geometry copy mismatch: $model"
    }
}
