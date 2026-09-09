# ZeroSlack Wave simulation

ZeroSlack owns Slang-derived RTL facts, selected-module manifests, source snapshots and source navigation.
WaveWorkbench owns Scenario editing, simulation execution, trace reading, comparison and shared waveform rendering.
Symbolic Preview and Simulated Result remain explicitly labeled; editing a symbolic preview does not run simulation.

## Data and runtime boundaries

- Module Manifest v5 carries stable module/symbol identities, parameters, ports, dependencies and source/driver links.
  Instance parameters use their effective values. Unresolved dependencies fail by default; explicitly selected
  passive input-only stubs permit compilation without claiming to simulate dependency behavior.
- Stimulus Scenario v5 and simulation session v2 preserve stable Scenario and target identity. WaveWorkbench
  validates stimulus and executes the existing runner. Explicit interfaces and structured port support remain
  bounded by the Wave manifest and simulation contracts; unsupported types fail rather than being guessed.
- Unsaved RTL is captured through the existing source-snapshot preparation path. Requests carry workspace,
  revision and generation identity. Late results cannot replace a newer selection or workspace.
- Actual VCD/FST traces and expected stimulus are separate facts. Internal trace-to-source navigation requires
  an unambiguous manifest mapping; missing identities do not trigger source guesses.
- Multi-scenario runs are serial, retain per-item results, continue after an item failure, and support stopping
  the active item and remaining queue. They reuse valid build cache entries.

## Toolchain and presentation

The portable toolchain contains Verilator 5.050, MinGW 13.1.0 and GNU Make. Discovery respects explicit external
configuration and the bundled sibling Toolchain directory. No system installation or environment-variable
rewrite is required. The bundle service prepares the toolchain lazily; missing tools produce actionable state.
Historic missing-Verilator fixture runs are not the current deployment contract.

`wave::WaveformView` renders symbolic payloads through `wave-preview/v1`; the complete Wave Simulation Workspace
has its own versioned ABI. Capability discovery, payload limits, generation rejection and host destruction are
part of the integration boundary. ZeroSlack does not keep a second waveform renderer.

Authoritative field-level contracts are maintained in the sibling WaveWorkbench repository:
`docs/integration-contracts.md`, `docs/simulation-runner.md`, `docs/project-format.md`,
`docs/fst-reader.md`, and `docs/wave-preview-v1.md`. Source schemas and existing integration tests govern
compatibility; a version change must update the contract and both consumers.
