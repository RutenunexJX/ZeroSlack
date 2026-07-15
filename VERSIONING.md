# ZeroSlack Versioning

`VERSION` is the single manually maintained product version source. It must
contain exactly one SemVer value in strict `X.Y.Z` numeric form.

Current controlled baseline: `v0.1.0`.

## Upgrade Rules

- `PATCH`: defect fixes and quality improvements to existing behavior.
- `MINOR`: complete new user features or user workflow extensions.
- `MAJOR`: stable compatibility commitments. Before `1.0.0`, ZeroSlack stays
  in the `0.x` line.

Dependency versions are tracked separately from the product version. The user
visible product version must not include dependency labels.

## Release Steps

1. Update `VERSION`.
2. Reconfigure CMake so `generated/version.h` is regenerated.
3. Build and run the release verification targets.
4. Create a signed-off release tag named `vX.Y.Z`.
5. Name the Windows package `ZeroSlack-X.Y.Z-win64`.
