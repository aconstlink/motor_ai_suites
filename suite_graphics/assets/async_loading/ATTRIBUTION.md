# Third-Party Model Credits

These assets are from the [Khronos glTF Sample Models repository](https://github.com/KhronosGroup/glTF-Sample-Models),
revision `d7a3cc8e51d7c573771ae77a57f16b0662a905c6`.
Khronos hosts the collection; the individual model authors and notices below
remain applicable. These models are not relicensed under this repository's MIT license.

## Sponza

- [Upstream model and notices](https://github.com/KhronosGroup/glTF-Sample-Models/tree/d7a3cc8e51d7c573771ae77a57f16b0662a905c6/2.0/Sponza).
- Improved Sponza model: Frank Meinl / Crytek; original Sponza: Marko Dabrovic.
- The upstream notice credits Morgan McGuire for model modifications (2011).
- Upstream also credits Alexandre Pestana's PBR textures; those images are not
  included in this texture-free sample.
- Original model/readme/licensing text is preserved in
  [Sponza/UPSTREAM_README.md](Sponza/UPSTREAM_README.md).

## Suzanne

- [Upstream model and notices](https://github.com/KhronosGroup/glTF-Sample-Models/tree/d7a3cc8e51d7c573771ae77a57f16b0662a905c6/2.0/Suzanne).
- Upstream notice: "Donated by Norbert Nopper for glTF testing."
- Preserved in [Suzanne/UPSTREAM_README.md](Suzanne/UPSTREAM_README.md).

The original READMEs contain relative screenshot links. Their screenshot folders
are not bundled; use the upstream links above to see those images.

## Local Adaptations

`prepare_assets.ps1` recreates these variants from a local copy of the source
repository. No network download is performed by the build or application.

- Remove texture/image/sampler declarations and material texture references.
- Add missing material names for Motor's current importer.
- List position and normal attributes first for the current D3D11 layout path.
- Add an `asset.copyright` pointer to these upstream notices.
- Copy each `.bin` unchanged and verify its SHA-256 against the source file.
- Preserve original base-color factors, transforms, accessors and mesh topology.

The original model notices are retained as supplied; no additional license or
copyright ownership by Khronos, Motor, or this sample's author is asserted.

```powershell
./prepare_assets.ps1 -Source E:/repositories_github/KhronosGroup/glTF-Sample-Models -Destination .
```
