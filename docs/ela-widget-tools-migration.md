# ElaWidgetTools UI migration

This document tracks the `codex/ela-widget-tools` branch. The branch is isolated from `main`; creating it does not change the released application.

The branch's formal package destination is
`E:/PinloomRoot/AppPackage/AppSuite-ElaWidgetTools`. Its packaging script
rejects the main branch's `AppSuite` package name, including when replacement
is requested. The first package from this branch uses version `0.29.12` and
ships the ElaWidgetTools DLL, its MIT notice, the Font Awesome font notice,
and the remaining application dependency license texts.

## First migration milestone

The fixed upstream library revision now builds as part of ZeroSlack. The
navigation rail and selected controls in Files, Design, Settings, Problems,
Activity, and scoped search use Ela widgets. Existing editor, graph views,
keyboard shortcuts, settings keys, and native window controls remain in place.
ZeroSlack's light, dark, and Catppuccin theme tokens are passed to Ela's theme
manager. The vendored icon font is registered before the main window is built.

The Qt 6.10.2 Release build of `demo` and five relevant GUI tests pass in the
offscreen test environment. GUI smoke screenshots were inspected for the
welcome page and settings page. Native desktop checks for snap, maximize,
hover, focus, animation, and high-DPI rendering remain necessary before a
release build is packaged.

## Compatibility gate

ZeroSlack's current Windows build uses Qt 6.10.2 (`build/Desktop_Qt_6_10_2_MinGW_64_bit-Release/CMakeCache.txt`). The upstream ElaWidgetTools `main` CMake configuration rejects Qt versions above 6.7.0 and forces its own Qt SDK path. The isolated library build at upstream revision `454cac2d57a47d3cc28577dc817793aec1881ca7` succeeded against Qt 6.10.2 after explicit `QChar` conversions for `ElaIconType::IconName` values. Application integration also builds and passes the GUI tests listed above, but native desktop checks are pending. The library links against `Qt6::WidgetsPrivate`, so its build must match the deployed Qt version exactly.

ElaWidgetTools is MIT-licensed. A distribution that includes its code or binaries must retain its license notice.

The upstream `ElaAwesome.ttf` icon font identifies its creators only as
"original authors @ fluttericon.com, fontello.com". Fontello states that
generated fonts retain their source icon licenses, but this ElaWidgetTools
revision does not contain the generated font's per-icon license manifest.
The vendored copy therefore replaces it with the unmodified Font Awesome
Free Solid 6.7.2 font under SIL OFL 1.1. Its license text is retained beside
the font and copied beside the application at build time. Ela's internally
referenced icons are remapped to the corresponding Free Solid glyphs; eleven
icons absent from Free Solid use documented free alternatives. The upstream
demo images were excluded from the vendored resource bundle.

## Migration boundaries

1. Integrate the verified ElaWidgetTools revision with ZeroSlack's CMake build. Keep the upstream revision and local `QChar` compatibility changes explicit.
2. Map Ela's light and dark theme colors to ZeroSlack's existing theme modes. Preserve editor syntax colors and graph semantic colors until each view has been inspected.
3. Migrate shared shell controls: title-bar actions, navigation controls, tabs, menus, search fields, buttons, lists, dialogs, and notifications. Retain existing action IDs, shortcuts, docking state, and settings keys.
4. Migrate the navigation pane, bottom panels, settings page, and welcome page. Check collapsed/expanded animation, focus, keyboard navigation, and high-DPI behavior.
5. Review specialized editor and graph surfaces individually. Their rendering and interaction should change only where Ela provides a demonstrable improvement without removing current capabilities.
6. Remove superseded application-wide QSS and `RoundedIcons::Style` rules only after their remaining responsibilities are covered by the new style.

## Acceptance checks

- Release build succeeds with the project's Qt 6.10.2 toolchain.
- Main-window minimize, maximize, restore, Windows snap, and panel docking work.
- Light, dark, and Catppuccin themes retain readable contrast in all migrated controls.
- File tree, editor tabs, settings, Problems, Activity, dialogs, and context menus remain usable with mouse and keyboard.
- Formatting, diagnostics, navigation, and specialized graph views retain their existing behavior.
- The installed Windows package includes any required ElaWidgetTools binaries
  and both ElaWidgetTools and Font Awesome license notices.

Upstream references: <https://github.com/Liniyous/ElaWidgetTools> and <https://github.com/Liniyous/ElaWidgetTools/blob/main/CMakeLists.txt>.
