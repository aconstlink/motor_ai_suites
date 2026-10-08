# Math Tests

Five independent console applications test Motor's math library without windows,
graphics backends or external assets. They use `motor::log`, Motor's memory dump
and ordinary runtime checks, so checks also execute in Release builds.

## Build and Run

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4 --target 00_vector 01_matrix 02_transformation 03_util 04_quaternion
ctest --test-dir build -C Release -L "^math$" --output-on-failure --no-tests=error
```

Replace `Release` with `Debug` for the debug configuration. Run from the
repository root. Each application also accepts no argument (all its cases),
`all`, or a single case name:

```powershell
./build/bin/Release/04_quaternion.exe
./build/bin/Release/04_quaternion.exe slerp
./build/bin/Release/02_transformation.exe camera
ctest --test-dir build -C Release -L "^math_matrix$" --output-on-failure
```

Single-configuration generators normally place executables directly under
`build/bin`; select the configuration with `-DCMAKE_BUILD_TYPE=Release` instead.
An unknown case/invalid argument count returns 2; a failed check or tracked leak
returns 1; success returns 0. Every CTest case has a 30-second timeout.

## Coverage

| Application | Cases | What is checked |
| --- | --- | --- |
| [00_vector](00_vector.cpp) | arithmetic, normalization, cross | vec2/3/4 component and scalar operations, compound operators, dot products, lengths, copy independence, normalized copies, zero vec3/4, cross-product orientation and self aliasing |
| [01_matrix](01_matrix.cpp) | products, transpose, homogeneous, rotation2 | 2x2/3x3/4x4 multiplication, scalar reference results, identity, `*=`, self aliasing, row/column access, transpose, homogeneous points versus directions, 2D rotations and angle extraction |
| [02_transformation](02_transformation.cpp) | trs, spaces, hierarchy, camera, projection | TRS order, positive nonuniform scales, translation replacement, left/right composition, three-level hierarchy, look-at/view inversion, -Z camera convention, perspective/orthographic near/far and screen edges, FOV conversion |
| [03_util](03_util.cpp) | scalar, rounding, angles, indexing, time, basis, collinear_basis | clamp/mix/smoothstep, ceil/floor/fract, negative-angle wrapping, nonsquare-grid indexing, millisecond decomposition across days, right-handed orthonormal bases and collinear-up fallback |
| [04_quaternion](04_quaternion.cpp) | axes, composition, matrix, slerp | +/-90 degrees around X/Y/Z, wxyz versus xyzw constructors, normalized axes, multiplication order, conjugate/inverse, normalization, Rodrigues reference, matrix conversion, q/-q equivalence, SLERP endpoints, shortest arc and near-equal fallback |

There are 23 separately registered CTest cases with the `math` label and an
additional `math_vector`, `math_matrix`, `math_transformation`, `math_util` or
`math_quaternion` label. Both CTest CI workflows build and run them in Debug
and Release. Failing cases are intentionally not skipped or marked `WILL_FAIL`.

## Conventions and Limits

- Matrix tests use row-major element storage and column-vector multiplication.
  Expected products are computed by a scalar loop, not another Motor multiply.
  Twelve deterministic, nonsymmetric signed fixtures exercise each matrix type.
- `mat4 * vec3` only uses the upper 3x3. Points are explicitly represented as
  `vec4(position, 1)`, directions as `vec4(direction, 0)`.
- TRS expectations use scale, then rotation, then translation. Parent transforms
  multiply on the left. These tests do not assume decomposition of shear or
  negative scale, or that `set_scale` preserves an existing arbitrary rotation.
- Camera tests use a right-handed frame looking along local -Z. Projection tests
  check the math library's OpenGL-style NDC depth [-1, 1], not API depth-buffer
  conversion. The perspective output matrix starts zero-initialized.
- Basis input directions are normalized. Zero directions are outside the tested
  contract. Collinear suggested up vectors are included because the implementation
  explicitly handles this case with a fallback.
- A default quaternion is zero, not identity. Tests use explicit `(1,0,0,0)`
  identity and never normalize a zero quaternion. The four-scalar constructor is
  wxyz; the vector constructor reads xyzw.
- Floating comparisons reject NaN/infinity and use an absolute-plus-relative
  tolerance of `2e-5 * (1 + abs(expected))`. Coverage primarily targets float;
  vector arithmetic and matrix products include selected double instantiations.
- These are correctness/regression tests, not performance measurements, exhaustive
  template coverage or GPU validation. No production Math source is modified.

## Initial Findings

Motor revision `0b82b20`, Windows/MSVC, 2026-10-08. Debug and Release both built
successfully and executed 23 cases with 3,745 individual checks per configuration.
Both runs found the same seven failing cases (118 failed individual checks);
the other sixteen cases passed. All 23 memory dumps per configuration were empty.
Linux/GCC and the hosted CI workflows have not been run locally.
Detailed build and test logs are kept locally in `build/math-build-*.log` and
`build/math-ctest-*.log`.

| Failing case | Observed result / source |
| --- | --- |
| `math_matrix_products` | `matrix2` and `matrix3` self multiplication (`a *= a`) reads the already modified right operand. Ordinary `a*b` and nonaliased `a*=b` pass the fixtures. See [matrix2](../motor/math/matrix/matrix2.hpp) and [matrix3](../motor/math/matrix/matrix3.hpp). |
| `math_matrix_transpose` | `matrix4::operator()(row,column)` indexes the opposite order from the named parameters and row/column APIs. Transposition and `get_column` are checked independently via raw elements. See [matrix4](../motor/math/matrix/matrix4.hpp). |
| `math_matrix_rotation2` | `matrix2::angle()` reports pi/2 for identity and pi for a pi/2 rotation. Its trace formula does not match a 2D rotation. |
| `math_util_rounding` | `fn::ceil(0/1/2)` returns 1/2/3. See [fn.hpp](../motor/math/utility/fn.hpp). |
| `math_util_angles` | `angle::constrain_angle(-pi/2)` returns approximately -7.854 instead of 4.712. Its `fn::mod` negative-input branch does not wrap into the stated interval. |
| `math_util_time` | At 24 hours, `milli_to` reports one day and hour 24. Hours use modulo 60 instead of 24. See [time.hpp](../motor/math/utility/time.hpp). |
| `math_util_collinear_basis` | With dir = +/-Z and suggested up = +Z, the fallback is also +Z. The resulting X/Y axes are zero. Collinear +/-Y with up = +Y passes. See [ortho_basis.hpp](../motor/math/utility/3d/ortho_basis.hpp). |

These failures will make the CTest workflow red until their implementation or
explicit API contract is corrected. Do not turn the tests green by replacing
mathematical expectations with the current incorrect outputs.
