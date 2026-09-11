# ZeroSlack Versioning

`VERSION` is the single manually maintained product version source. It must
contain exactly one SemVer value in strict `X.Y.Z` numeric form.

Current controlled baseline: `v0.25.9`.

## Upgrade Rules

- Every accepted code delivery must increment the product version before it is
  pushed or packaged; use at least a `PATCH` increment even when the delivery
  contains only defect fixes.
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
5. Publish Windows artifacts under
   `E:\PinloomRoot\AppPackage\AppSuite`; the path intentionally contains no
   spaces.
6. Keep both the Windows package directory and archive basename fixed as
   `ZeroSlack-win64`; do not include the product version in either package
   filename so existing shortcuts remain valid.

The product version is recorded only in `VERSION`, the application display,
`CHANGELOG.md`, and the release tag.
