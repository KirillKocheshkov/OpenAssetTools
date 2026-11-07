# IW3: xmodel collision generation (issue #812)

## Problem

When building an IW3 xmodel from `.glb`/JSON, the loader did not fill `XModel::collSurfs`, `numCollSurfs` and `contents`. The model rendered and animated normally, but bullets passed through it (rigid models with `"collLod"`, for example zombie).

## What was done

IW3 only (`#ifdef FEATURE_IW3`); other games are unaffected.

| File | Change |
|---|---|
| `src/ObjLoading/XModel/LoaderXModel.cpp.template` | `BuildCollisionSurfaces` and helper functions; called from `LoadLod` for the lod given by `collLod` |
| `test/SystemTests/Game/IW3/XModelCollSurfIW3.cpp` | Test: a single triangle with hand-computed plane/svec/tvec |

No change to Unlinker was needed: it dumps the mesh, and collision is rebuilt when packing.

## How collision is built

For each triangle of the collLod that is rigidly attached to one bone (otherwise it is skipped), `plane`, `svec` and `tvec` are computed. Triangles are grouped into one `collSurf` per (bone, material) pair.

```
e1 = v1 - v0;  e2 = v2 - v0
N  = normalize(cross(e2, e1));  d = dot(v0, N)          // plane = (N, d)
Cs = cross(N, e2);  K1s = 0.5*dot(v0+v2, Cs);  Ks = K1s - dot(v1, Cs)
svec = (-Cs/Ks, -K1s/Ks)                                  // xyz negated, w is not
Ct = cross(N, e1);  K1t = 0.5*dot(v0+v1, Ct);  Kt = K1t - dot(v2, Ct)
tvec = (-Ct/Kt, -K1t/Kt)
```

## Key decisions and why

All values were verified by comparison against real retail collSurfs (an unpacked `.ff`, 39,900 triangles of the zombie model), not only by internal consistency of the formula.

- **svec/tvec sign.** The engine evaluates `s = w - dot(xyz, p)`. With the opposite sign of xyz, the hit test is mirrored and bullets register only at scattered spots. This was the decisive bug.
- **Model-space coordinates.** Vertices are used as-is, without conversion into bone space (retail stores them this way, with non-zero bone offsets and identity rotations).
- **`contents = 1`** for every collSurf (as in retail; the Quake III `CONTENTS_BODY` value is wrong).
- **`surfFlags = surfaceTypeIndex << 20`**, where index = `ctz(material->info.surfaceTypeBits) + 1` (0 means no surface type). For example `surfaceTypeBits = 64` gives `0x700000` (flesh), matching retail. Low bits seen on some retail models (for example `0x20`) are not reproduced.
- **Diagnostics.** The build writes a summary to `build_result.txt` next to the executable (face count, skipped faces, buckets, rejected triangles).

## Limitations

- IW3 only, and only the lod given by `collLod`.
- Triangles with several bone weights or spanning different bones are skipped; if all are skipped, the model has no collision (`con::error`).
- Verified in game on zombie (hits register on all body parts, including the head). Other rigid models are built by the same code but were not tested individually.

## Verification

Run the full `ObjLoadingTests` and `SystemTests` suites, then `SystemTests "[collision]"`. To check on a real model: build an `.ff` and compare its collSurf records (44 bytes: `collTris`, `numCollTris`, mins[3], maxs[3], `boneIdx`, `contents`, `surfFlags`; followed by triangles of 48 bytes: plane[4], svec[4], tvec[4]) against a retail zone.
