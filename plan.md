# ZeroSlack Current Plan

Product version: `v0.25.4`

## Current baseline

Current behavior is documented in the README and user manual. Released changes
and completed implementation history are recorded in CHANGELOG.md and Git.

## Repository maintenance

- Classify first-party sources under `src/`; keep CMake and source-policy checks aligned.
- Remove superseded local build/package outputs and completed historical documents.
- Keep the active Release build, current release evidence, toolchain inputs and referenced test fixtures.
- See [repository layout](docs/repository-layout.md) for ownership and placement rules.

## Remaining validation

- Manually verify Windows edge dragging and mixed-DPI multi-monitor behavior;
  keyboard snapping and title-bar mouse operations already have regression coverage.

## Maintenance rules

- Select one specific usability issue before starting another implementation slice.
- Keep README, manual, changelog and package metadata aligned with VERSION.
- Preserve semantic ownership, generation checks and read-only CLI boundaries.
- Run tests relevant to changed behavior; record the configuration and date.

## Contracts

- [Architecture](ARCHITECTURE.md)
- [Suite protocol](docs/suite-app-protocol.md)
- [Suite workflows](docs/suite-workflows.md)
- [Visual system](docs/suite-ui.md)
- [CLI](docs/cli.md)
- [Wave simulation](docs/wave-simulation.md)
