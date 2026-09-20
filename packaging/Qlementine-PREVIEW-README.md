# ZeroSlack Qlementine preview

Run `ZeroSlack-Qlementine-Preview.exe`. This package is independent of the
installed ZeroSlack and does not replace the formal package.

- The default preview style is Qlementine; `--ui-style=classic` starts a
  comparison session. Restart to change style backends.
- `--no-ui-animations` disables Qlementine control animation.
- Global configuration, recent projects, recovery data and workspace-session
  storage use the `ZeroSlack-Qlementine-Preview` application profile. Existing
  ZeroSlack global configuration is not migrated. Source files and workspace
  `.zeroslack` settings still belong to whichever project you choose to open.
- SuiteRuntime automatic registration is disabled in the preview executable.
- Editor, radial context menu and specialized graph renderers retain their
  existing implementation. In SuiteUi builds, only QPushButton, QToolButton
  and QCheckBox use its renderer; other controls retain the application style.
- This is a validation build. Cross-monitor DPI, DWM frame pacing and the
  subjective quality of animation require independent desktop acceptance.

Keep the executable, DLLs, plugin directories and licenses together. Qt is
dynamically linked and can be replaced with a compatible Qt build. Qt sources:
https://download.qt.io/archive/qt/6.10/6.10.2/submodules/
See `THIRD-PARTY-NOTICES.md` and `licenses/` for attribution and full texts,
including Qlementine, Inter, Roboto Mono, Qt and the MinGW runtime exception.

Built from the working tree based on ZeroSlack 0.29.21. Qlementine is pinned to
`209e549c415f1d828883f9f21a633eb535f9d67d`, with the documented `stopAll` fix.
`SHA256SUMS.txt` records this package's files.

When built with `ZEROSLACK_ENABLE_SUITEUI=ON`, the installed SuiteUi 0.1.0
static package supplies the private Qlementine backend. Its exact toolchain,
resources, patches and licenses are in `licenses/SuiteUi/`. No separate SuiteUi
DLL or style plugin is required. The executable name and isolated preview
profile are unchanged. The vendored backend remains a separate build option.
