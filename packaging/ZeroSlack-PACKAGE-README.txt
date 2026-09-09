ZeroSlack v0.22.1

Build profile: Shared Release
Qt: 6.10.2
Compiler: MinGW 13.1.0
slang: 10.0.14
Suite WaveWorkbench: v0.12.0
Bundled Wave toolchain: Verilator 5.050, MinGW 13.1.0, GNU Make
Release root: E:\PinloomRoot\AppPackage\AppSuite

Run ZeroSlack.exe. Keep the Runtime, WaveWorkbench, and Toolchain directories
beside the ZeroSlack-win64 directory under AppSuite\Apps. Runtime provides the
shared SuiteApp broker; WaveWorkbench provides waveform views and simulation;
Toolchain contains wave-toolchain-v1.zip and wave-toolchain-bundle.json. The
first simulation verifies and extracts this bundle into the local
application-data cache; later runs reuse it. The target computer does not need
a separate Verilator or C++ compiler installation or system PATH changes.

The package directory and executable names remain fixed so existing Windows
shortcuts continue to work across upgrades. Package, toolchain, build-cache,
staging, and Verilator object directory names supplied by this release contain
no spaces. User source paths remain supported through quoted process arguments.

External Verilator and compiler executables may still be selected under
Settings > Simulation. Empty settings prefer an existing expanded portable
toolchain, then the verified bundle cache, environment variables, and PATH.
Legacy standalone ZeroSlack packages remain supported.

Activity replaces the status bar. Scan, semantic and operation messages are
retained in Activity. Important unread messages increment its numeric badge;
viewing Activity marks displayed messages as read. Ordinary progress stays passive.
