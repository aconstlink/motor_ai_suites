# Stanford Scan Models

These assets are third-party scan data, **not covered by the source-code license
of motor_ai_suites**. They are included for a non-commercial graphics research
and culling demonstration. Consult the original terms before reuse, especially
in a commercial product.

## Sources and Credits

- **Happy Buddha, Dragon, Lucy:** Stanford University Computer Graphics Laboratory,
  [Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/).
- **Thai Statue:** XYZ RGB Inc., distributed through the same Stanford repository.
  Stanford credits Helmut Kungl for the XYZ RGB models.
- The reduced Buddha and Lucy OBJ sources were obtained from Alec Jacobson's
  [common-3d-test-models collection](https://github.com/alecjacobson/common-3d-test-models),
  revision `8a4f8642acaf43f9cd7b67858a1502e1055ef202`, files `data/happy.obj` and
  `data/lucy.obj`. The collection is a distribution source, not the scan creator.
- Dragon source: `dragon_vrip_res2.ply` in Stanford's
  [dragon_recon.tar.gz](https://graphics.stanford.edu/pub/3Dscanrep/dragon/dragon_recon.tar.gz).
- Thai Statue source:
  [xyzrgb_statuette.ply.gz](https://graphics.stanford.edu/data/3Dscanrep/xyzrgb/xyzrgb_statuette.ply.gz).

Stanford permits research use and redistribution with attribution, but requests
permission for commercial use and respectful treatment of the scanned objects.
The demo keeps the sculptures static, with fixed orientation corrections,
uniform size normalization and translation for display. It does not deform or
animate their geometry.
The [original repository terms](https://graphics.stanford.edu/data/3Dscanrep/)
are authoritative; this note does not grant additional rights.

## Local Preparation

Prepared on 2026-11-06 with Blender 5.1.1, offline:

1. Import the OBJ/PLY source; orient upright.
2. Apply a Decimate modifier targeting approximately 100,000 triangles if needed.
3. Export triangulated, Y-up OBJ with positions and faces only, without materials,
   UVs or stored normals. Each file is approximately 3.4-3.7 MB.

The application imports these files through motor's format registry and IO
database, computes area-weighted smooth vertex normals, and derives bounds from
referenced vertices. No Blender installation is needed to run it.

The large source archives and full-resolution meshes are not part of this repo.
