ZeroSlack v0.17.0

Build date: 2026-08-20
Build profile: Shared Release
Qt: 6.10.2
Compiler: MinGW 13.1.0
slang: 10.0.14
Bundled WaveWorkbench: v0.11.0 (revision 76fbc9f)
Bundled Wave toolchain: Verilator 5.050, MinGW 13.1.0, GNU Make

Run ZeroSlack.exe. Keep the WaveWorkbench subdirectory beside ZeroSlack.exe;
it provides the embedded Wave workspace, simulation runner, CLI, schemas,
optional FST reader, and self-contained simulation toolchain. The target
computer does not need a separate Verilator or C++ compiler installation and
does not need system PATH changes.

The package directory and executable names remain fixed so existing Windows
shortcuts continue to work across upgrades. Package, toolchain, build-cache,
staging, and Verilator object directory names supplied by this release contain
no spaces. User source paths remain supported through quoted process arguments.

External Verilator and compiler executables may still be selected under
Settings > Simulation. Empty settings prefer the bundled toolchain, then check
environment variables and PATH.
