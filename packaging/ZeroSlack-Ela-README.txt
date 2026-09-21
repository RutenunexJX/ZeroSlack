ZeroSlack Ela Windows package

This is the independent Ela branch application. Launch ZeroSlack-Ela.exe.
It does not replace the classic formal package. The fixed package directory is
ZeroSlack-Ela-win64. Consult build-info.json for the product version, release
channel, exact commit and branch. Formal packages have dirty=false and use
the release tag ela-vX.Y.Z.

Settings use an isolated INI profile under Qt's AppConfigLocation for
ZeroSlack/ZeroSlack-Ela. Recent workspaces, editor settings and workspace sessions
do not share the classic application's user profile. Workspace-local files are
still shared if you explicitly open and change the same workspace.

Use ZeroSlack-Ela.exe --ui-style=classic for the original controls in this same
isolated profile. Changing the backend requires restarting the application.

Ela controls: welcome page, settings, workspace configuration, navigation filters, Problems,
Activity, workspace hub and context panel controls. Existing window management,
editor, specialized diagrams and their data models remain in use.

The application links Qt and ElaWidgetTools dynamically. Keep the distributed
DLLs and platform plugins with the executable. Do not substitute another Qt
version: this Ela source uses private Qt 6.10.2 headers.

Licenses and exact Ela provenance/patches are under licenses/. See also LICENSE
and THIRD-PARTY-NOTICES.md. ElaWidgetTools is MIT; its unmodified Font Awesome
Free Solid 6.7.2 font is SIL OFL 1.1. ZeroSlack remains Apache-2.0.
