# Welcome illustration resources (version 1)

These files control all three welcome illustrations and their motion. ZeroSlack loads
them once from **the executable's directory**, never from the working directory.
No compiler, plugin rebuild, network access, or live reload is involved.

```
ZeroSlack.exe
resources/welcome/
  manifest.json
  rtl.scene.json          # RTL Development
  waveform.scene.json     # Waveform Design (entry remains unavailable)
  registers.scene.json    # Register Design (entry remains unavailable)
  Update-WelcomeChecksums.ps1
  examples/alternate/     # complete replacement manifest and three scenes
  LICENSE
  NOTICE.txt
```

## Replace the illustrations

1. Close ZeroSlack. Back up the **whole** `resources/welcome` directory outside
   the application folder; keep its manifest together with its scene files.
2. Edit the three scene files, or replace `manifest.json` and all files it names.
   To try the supplied alternate design, copy the four JSON files from
   `examples/alternate/` into `resources/welcome/`, replacing the existing files.
3. If you edited any scene bytes or changed the manifest's paths, run:
   `pwsh -NoProfile -File "<app>/resources/welcome/Update-WelcomeChecksums.ps1"` (PowerShell 7).
   An intact example bundle already has valid scene hashes; the helper also updates
   resource entries in an installed package's `SHA256SUMS.txt` when present.
   It preserves every EXE/DLL checksum and backs up the preceding manifest as
   `manifest.json.previous`. It checks JSON, version and paths; the host performs
   full scene validation. A hash is an integrity check, not a signature.
4. Restart ZeroSlack, then hover each card. All three illustration resources can
   change, including those whose document entry is unavailable. Titles, descriptions,
   click actions and availability are controlled by the host and do not change.

The default scenes are application-owned. A replaced bundle is user-owned; keep
your backup separately. The build target refreshes **development build** resources
from this repository, so do not use that directory to store your only customized copy.
Packaging creates a new directory and never merges scenes or overwrites an installed
bundle. During an application upgrade, back up the installed bundle first and either
restore it into the new package (if version 1 is still supported), or pass its directory
to `scripts/package-release.ps1 -WelcomeResourceDirectory <backup>` when preparing
the new package. Do not silently mix files from two bundles. Keep the old application
and bundle backup until the replacement has been checked. `build-info.json` records
the initially packaged manifest; its provenance is intentionally not rewritten by
later user resource edits.

## Manifest and scene format

All JSON is UTF-8. The manifest uses `format: "zeroslack.welcome.bundle"`, `version: 1`,
and a `cards` object with stable keys `rtl`, `waveform`, `registers`. Each entry has
`scene` (a relative path under this directory) and `sha256` (64 hexadecimal digits
over the scene file's exact bytes). Absolute paths, URLs, `..`, backslashes and
paths resolving outside the bundle are rejected. The helper also rejects linked
entries. Scenes are independently validated: one bad scene does not disable the others.

Each scene has `format: "zeroslack.welcome.scene"`, `version: 1`, `canvas: [width,height]`,
`durationMs`, an `accent` hexadecimal color, and a nonempty `layers` array.
The canvas is fitted and clipped into the existing card's illustration area, with
host upscaling capped at 1.12. Its
coordinates are logical vector units, not physical display pixels. Default canvas:
230×180, duration: 2400 ms. Base property values define the **static** pose; tracks
are evaluated only while a hover animation is running. Completion restores the
static pose. No looping or automatic resume is performed.

Layer types and fields:

| Type | Geometry/content |
|---|---|
| `rect` | `x`, `y`, `width`, `height`, optional `radius` |
| `ellipse` | bounding `x`, `y`, `width`, `height` |
| `line` | from `(x,y)` to `(x+width,y+height)` |
| `path` | `path: [["M",x,y],["L",x,y],["Q",cx,cy,x,y],["C",c1x,c1y,c2x,c2y,x,y],["Z"]]`; first command must be M |
| `text` | `text`, `x`, `y`, optional `fontFamily` (default Consolas), `fontSize` (points, default 9), `characters`; height 0 uses baseline coordinates, otherwise `width`/`height` and `align: left/center/right` use a vertically centered box |
| `group` | nested `layers`; common transforms and opacity apply to its descendants |

Every layer supports `fill`, `stroke`, `strokeWidth`, `fillOpacity`, `strokeOpacity`,
`opacity`, `translateX`, `translateY`, `scaleX`, `scaleY`, `rotation` (degrees).
Transforms apply in translation/rotation/scale order around the local origin.
Path and group positioning uses transforms; x/y geometry applies to the other types.
Default fill/stroke is `none`, opacity and scale are 1, strokeWidth is 1; other
numeric values are 0. Text uses its stroke color. `characters` defaults to all text
and counts Unicode grapheme clusters. Font files are not loaded from resources.

Colors: `#RRGGBB`, `#AARRGGBB`, `none`, or theme tokens `@ink`, `@base`, `@muted`,
`@accent`, `@card`. The first three follow the host palette; accent is the scene
accent and card is the host's tinted background. Opacity values are 0–1.

Any numeric property above except fontSize can have a track. Example:

```json
{"type":"rect","x":20,"y":40,"width":160,"height":80,"radius":10,
 "fill":"@accent","fillOpacity":0.15,"stroke":"@ink",
 "tracks":{"translateX":{"mode":"linear","keys":[[0,-10],[0.5,10],[1,-10]]}}}
```

Each track has 2–64 strictly increasing `[time,value]` pairs, including times 0 and 1.
Mode is `linear` (default) or `step` (hold the preceding value). Supported track names:
`x`, `y`, `width`, `height`, `radius`, `translateX`, `translateY`, `scaleX`, `scaleY`,
`rotation`, `opacity`, `fillOpacity`, `strokeOpacity`, `strokeWidth`, `characters`.
Use transforms to animate paths; path morphing, expressions, scripts, image/video
loading, external fonts and SVG animation are not part of version 1.

Limits: manifest 64 KiB; each scene 256 KiB; canvas dimensions 1–1024; integer duration
100–10000 ms; 256 total layers; 8 nested group levels; 8192 total path commands;
512 tracks; 1024 UTF-16 text units per layer and 8192 total; font family 64 characters;
font size 4–72 pt. Numeric geometry/translation/rotation must be finite and within
±4096; dimensions/radius are nonnegative; scales 0.01–8 (combined ancestor scales,
including all keyframes, must also remain at most 8); strokeWidth 0–32;
characters 0–1024. Unknown format versions, layer types and track properties fail
validation. An oversized/malformed resource never runs code.

## Playback, fallback and recovery

Playback starts on pointer entry, respecting the application's animation switch.
Leaving the card, hiding the welcome page, opening a workspace, window deactivation,
minimization, closing or destroying the window stops it. Turning animations back on
does not restart a stopped card until the pointer enters again. Disabled document
entries may still animate on hover; their click behavior remains disabled.

Missing, corrupt, incompatible or mismatched resources use one neutral static
placeholder while the welcome page and workspace actions remain usable. Diagnostics
are appended once per process to **Activity > Welcome resources**, with the card/path
and cause; no repeated popups or per-frame disk reads occur. Fix the files and hashes,
or restore your complete backup, then restart to recover. Files replaced while the
application is open are intentionally not reloaded.

The supplied drawings/examples use the application's Apache-2.0 license. Include
LICENSE and NOTICE.txt when redistributing this bundle. Include suitable notices and
permissions for any artwork you replace them with.
