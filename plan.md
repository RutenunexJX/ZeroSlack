# ZeroSlack Development Plan

Use `readme.md` for the current handoff baseline and `goal.md` for the active
long-term goal model. This file defines how to execute work.

## Current Identity

ZeroSlack is a SystemVerilog code editor and workspace browser. It should help
users read, navigate, edit, and understand RTL projects without turning UI code
into an analyzer.

## Non-Negotiable Architecture

- Use Qt 6, CMake, and Ninja only.
- Do not restore qmake, `*.pro`, `*.pri`, `.claude`, SVLexer, the old
  Tree-sitter symbol parser, the Tree-sitter verify button, regex relationship
  analysis, or long-lived scattered perflog probes.
- Do not revive `;:cmd` or introduce active `;:` behavior.
- UI code must not run Slang, scan workspace files, or own semantic policy.
- New semantic/RTL behavior must flow through:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

- Tree-sitter may be used for editor-local syntax structure and low-latency
  insertion/navigation targets.
- Slang-backed semantic truth must enter UI features through semantic records,
  snapshots, query services, or feature services.
- Keep command routing separate:
  - Global Control: app/workspace/global domains.
  - COM Mode: current-editor local commands.
  - `;cmd`: semantic command completion.
  - `;;cmd`: code template expansion.

## Worktree Policy

- Start every turn by checking the current worktree state.
- Never revert user changes unless explicitly asked.
- If unrelated dirty files exist, leave them alone and stage only the files for
  the current milestone.
- Documentation cleanup may be committed separately from implementation changes.
- Do not push unless explicitly asked or unless the active goal milestone rules
  for the current milestone explicitly require it.

## Active Long-Term Scope

Only these long-term tracks are allowed. Do not add extra tracks without an
explicit user request.

### 1. Editor Daily Operations Completion

Goal: make common editing actions easy to reach and consistent.

Allowed scope:

- replace
- goto
- comment
- uncomment
- indent
- unindent
- clear entry points for existing partial command/action paths

First milestones:

- M1.1 inventory existing actions and shortcuts, document gaps, no behavior
  change
  (complete: baseline captured for find, formatter, alternate commands, and
  missing daily action entry points)
- M1.2 add reliable entry points for one action family, with focused editor
  tests
  (complete: comment/uncomment are available through `Ctrl+/`,
  `Ctrl+Shift+/`, editor context-menu actions, and alternate commands)
- M1.3 repeat per action family until all listed actions have usable entries
  (complete: indent/unindent are available through `Ctrl+]`, `Ctrl+[`, editor
  context-menu actions, and alternate commands)
- M1.4 add reliable replace and goto line entry points, with focused editor
  tests
  (complete: replace uses `Ctrl+H`, context menu, and `replace`; goto line uses
  `Ctrl+G`, context menu, and `goto_line`)

### 2. Slot Mode After Template Insertion

Goal: after `;;cmd` template insertion, users can fill multiple editable points
without manually moving the cursor.

Allowed scope:

- editable slots in inserted templates
- jump between slots
- exit/cancel behavior
- integration with `;;cmd`

First milestones:

- M2.1 define slot model and editor state without changing templates
- M2.2 enable slots for one template family
- M2.3 expand slots to existing signal/parameter template families

### 3. Batch RTL Edit Actions

Goal: support focused RTL batch edits that reduce repetitive cleanup.

Allowed scope:

- first target: clear assignment RHS and create fill slots
- example: `a <= xxx; b <= yyy;` becomes `a <= ; b <= ;`
- slot flow after the batch edit

First milestones:

- M3.1 service/report design for selected assignment cleanup
- M3.2 editor command for clear-RHS on selected assignments
- M3.3 connect result to slot mode

### 4. COM Mode Framework Completion

Goal: improve COM Mode quality before adding more commands.

Allowed scope:

- unified command registry
- help and hints
- conflict checks
- failure reason display
- command metadata
- existing g-domain preservation

First milestones:

- M4.1 introduce registry metadata for existing commands and prefixes
  (complete: `commodecommandregistry` now owns existing command metadata)
- M4.2 add help/hint rendering in the command strip or picker
  (complete: command strip renders registry-backed prefix and line hints)
- M4.3 centralize conflict validation and failure messages
  (complete: registry validates conflicts and supplies COM failure messages)

### 5. Limited Workspace Workflow Additions

Goal: improve workspace workflow only in the explicitly allowed ways.

Allowed scope:

- ignored directories
- restore Global Control `ow r` for recent workspaces

Not in scope:

- session restore
- include dirs/defines configuration UI
- recent files
- broad workspace UX expansion

First milestones:

- M5.1 document and audit current workspace open/recent behavior
- M5.2 add ignored-directory model/service path
- M5.3 restore `ow r` as a displayed Global Control child command

### 6. Completion / `;cmd` / `;;cmd`

Goal: make completion commands customizable and clearly separated from COM Mode.

Allowed scope:

- user templates
- custom abbreviations
- slot mode integration
- responsibility boundary between `;cmd`, `;;cmd`, and COM Mode

First milestones:

- M6.1 document current command/template responsibilities
- M6.2 add user-template storage/query model
- M6.3 add custom abbreviation resolution
- M6.4 integrate user templates with slot mode

### 7. Fold / Fold Shelf

Goal: make Fold Shelf durable and maintainable.

Allowed scope:

- persistence
- cross-file restore
- rename shelf item
- search shelf item
- clean shelf item

First milestones:

- M7.1 define shelf persistence model
- M7.2 persist and restore same-file items
- M7.3 cross-file restore
- M7.4 rename/search/clean management actions

### 8. Signal Kernel Graph

Goal: make dense signal graphs usable.

Allowed scope:

- high fanout collapse/grouping
- filtering
- in-graph search

First milestones:

- M8.1 fanout grouping model in service/report layer
- M8.2 UI collapse/expand for grouped fanout
- M8.3 filtering and in-graph search

### 9. Wave Preview

Goal: show a waveform sketch for selected RTL context only.

Allowed scope:

- selected `always` block waveform sketch
- selected module waveform sketch
- no simulation

Not in scope:

- value simulation
- testbench execution
- waveform database import
- timing-accurate verification

First milestones:

- M9.1 selected-always entry and report refresh
- M9.2 selected-module entry and scoped report
- M9.3 UI polish for sketch readability

### 10. State Transition Graph

Goal: show state transition graph only for selected next-state variables.

Allowed scope:

- trigger only when selected identifier is `ns` or `next_state`
- selected `cs` / `current_state` must not show the graph
- service-owned transition extraction/report

First milestones:

- M10.1 trigger gating test for `ns` / `next_state` only
- M10.2 transition report service
- M10.3 graph UI rendering and navigation evidence

### 11. Module Block Diagram

Goal: show module containment as a block diagram from a selected module.

Allowed scope:

- selected module name as diagram top
- module/interface instance wrapping relationship only
- no signals
- click module block to jump to module definition

First milestones:

- M11.1 service report for module-instance containment from selected module
- M11.2 render module-only block diagram
- M11.3 click navigation to module definitions

## Huge Workspace Status Audit

Huge Workspace is not an active UX expansion track in this plan. Only audit and
document whether existing low-level strategies still work:

- current/open/dirty-open priority
- analysis-band metadata and query ordering
- stale request coalescing/expiration
- cancellation boundaries
- staged publication
- Activity/status telemetry
- Release performance references for `huge_prj`

Audit milestones:

- HWA.1 list current strategy owners and tests/harnesses
- HWA.2 run or compile appropriate verification based on environment safety
- HWA.3 update docs with confirmed status and gaps

## Milestone Definition Of Done

Every implementation milestone must be small, verifiable, and deliverable.

Before a milestone is complete:

- update `readme.md`, `plan.md`, and `goal.md`
- run verification appropriate to the change
- keep UI semantic policy out of UI code
- do not add functionality outside the allowed long-term scope
- commit and push the milestone when the user has authorized that goal flow

Documentation-only milestones:

- update the relevant docs
- run `git diff --check`
- commit/push only if the user explicitly asks or the active goal flow requires
  this documentation milestone to be published

## Verification Guidance

- Docs only: `git diff --check`.
- Editor-only compile-safe changes: build affected targets such as
  `completion_test`, `ts_doc_test`, or `gui_smoke_test` without launching known
  problematic executables.
- Shared semantic/scheduler/project/snapshot changes: full Ninja and full CTest
  unless executable tests are blocked by the user/environment.
- During CTest, if execution stalls or runs much longer than expected, inspect
  the desktop for a Windows application-error or memory-read dialog before
  treating the run as ordinary long-running test work.
- Visual panels: verify service reports first, then UI smoke if executable runs
  are safe.

## Commit Policy

- Keep commits coherent and architecture-oriented.
- Do not mix unrelated dirty code into documentation commits.
- Do not push unless explicitly asked or required by the active milestone goal
  flow.
- Prefer short commit messages that name the product/architecture change.
