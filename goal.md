# ZeroSlack Long-Term Goal Model

ZeroSlack is a SystemVerilog code editor and workspace browser. Long-term work
must improve daily RTL editing and understanding while preserving the service
boundary:

```text
ProjectModel / DocumentModel / SemanticIndexSnapshot
  -> Query Service or Feature Service
  -> Report / Model
  -> UI render
```

## Goal Mode Rules

- Work only on the goal tracks listed in this file.
- Each milestone must be small, verifiable, and deliverable.
- Each completed milestone must update `readme.md`, `plan.md`, and `goal.md`.
- Each completed milestone must run verification appropriate to the changed
  scope.
- Each completed milestone must be committed and pushed.
- Do not add unlisted features to a milestone.
- Do not put semantic policy, workspace scans, or Slang execution in UI code.
- Do not restore qmake/pro/pri, SVLexer, old Tree-sitter symbol parsing, regex
  relationship analysis, or active `;:` commands.
- Huge Workspace work is status audit only unless the user explicitly changes
  the scope.

## Current Goal State

Current milestone: `G2.2 Parameter Template Slot Mode`.

Status:

- G0 Documentation And Goal Reset is complete and pushed in commit `bc2c059`.
- G4.1 Registry Metadata For Existing COM Commands is complete:
  `commodecommandregistry` now owns metadata for fixed executable commands,
  non-executable prefixes, and the module-relative line command.
- Existing COM command behavior is preserved; editor runtime parsing now reads
  executable commands, prefixes, and `g<num>` checks from the shared registry
  API.
- G4.2 Help And Hint Rendering For COM Commands is complete:
  `commodecommandregistry` exposes hint text for executable commands,
  non-executable prefixes, and module-relative line buffers; the app-level
  command strip renders those hints.
- G4.3 Centralized Conflict Validation And Failure Reason Display is complete:
  registry validation can be tested against injected metadata, reports duplicate
  command, executable prefix conflict, and malformed prefix reasons, and editor
  COM failures use registry-backed messages where practical.
- Focused verification for G4.3: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.1 Editor Daily Action Inventory is complete: the baseline captured
  `Ctrl+F` Find, formatter context-menu actions, alternate command metadata,
  and the missing daily action entry points.
- G1.2 Comment And Uncomment Entry Points is complete:
  active-line and selected-line line comments are available through `Ctrl+/`,
  `Ctrl+Shift+/`, editor context-menu actions, and alternate commands
  `comment` / `uncomment`, without reviving active `;:` commands.
- Focused verification for G1.2: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.3 Indent And Unindent Entry Points is complete:
  active-line and selected-line indentation is available through `Ctrl+]`,
  `Ctrl+[`, editor context-menu actions, and alternate commands `indent` /
  `unindent`, without reviving active `;:` commands.
- Focused verification for G1.3: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G1.4 Replace And Goto Line Entry Points is complete:
  replace is available through `Ctrl+H`, editor context-menu action, and
  alternate command `replace`; goto line is available through `Ctrl+G`, editor
  context-menu action, and alternate command `goto_line`; non-dialog editor API
  coverage checks valid/invalid goto line, replace-next, selected replacement,
  replace-all, and replace-all undo behavior.
- Focused verification for G1.4: Release `gui_smoke_test` target compile/link
  passed without launching the executable.
- G2.1 Slot Mode Model And Editor State is complete: Slot Mode ownership is
  documented as template-relative slot metadata from `CodeTemplateService`,
  insertion/start routing through `EditorCompletionWorkflow`, and editor-local
  session ownership in `MyCodeEditor` state; entry, Tab/Shift+Tab navigation,
  completion, Esc cancel, natural exit, stale-session invalidation, and future
  verification cases are defined without changing template behavior.
- Focused verification for G2.1: documentation inspection plus `git diff --check`.
- Verification flow now includes checking for external Windows application-error
  or memory-read dialogs when CTest appears stalled.

Completion criteria for G2.2:

- add concrete slot metadata for `;;p` / `;;lp` parameter declaration templates
- start Slot Mode after parameter template activation through the existing
  completion workflow
- implement editor-local Slot Mode session state for ordered ranges,
  highlighting, Tab/Shift+Tab navigation, completion, Esc cancel, and stale
  session exit
- preserve current template insertion behavior for all non-parameter templates
- `readme.md`, `plan.md`, and `goal.md` are updated
- appropriate focused verification passes
- milestone commit is pushed

## Track 1: Editor Daily Operations Completion

Goal: provide practical entry points for daily editing actions.

Allowed actions:

- replace
- goto
- comment
- uncomment
- indent
- unindent

Milestones:

- G1.1 Inventory current commands/actions and document missing entry points.
- G1.2 Deliver one coherent action family with tests.
- G1.3 Repeat until all listed daily actions have usable entry points.
- G1.4 Replace and goto line entry points.

## Track 2: Slot Mode After Template Insertion

Goal: after `;;cmd` insertion, users can jump through editable slots instead of
manually moving the cursor.

Milestones:

- G2.1 Define slot model, editor state, cancel/exit behavior, and verification
  cases.
- G2.2 Enable slot mode for the parameter declaration template family.
- G2.3 Expand slot mode to remaining signal/parameter template families.

## Track 3: Batch RTL Edit Actions

Goal: add focused RTL batch edits that feed slot mode.

First target:

- clear assignment RHS expressions and create fill points
- example: `a <= xxx; b <= yyy;` becomes `a <= ; b <= ;`

Milestones:

- G3.1 Design the clear-RHS report/service path.
- G3.2 Implement the editor-local clear-RHS command for selected assignments.
- G3.3 Connect clear-RHS output to slot mode.

## Track 4: COM Mode Framework Completion

Goal: improve COM Mode framework quality before adding more commands.

Allowed work:

- unified command registry
- help/hints
- conflict handling
- failure reason display
- metadata for existing commands and prefixes

Milestones:

- G4.1 Registry metadata for existing commands and prefixes.
- G4.2 Help/hint rendering for commands and prefixes.
- G4.3 Centralized conflict validation and failure reason display.

## Track 5: Limited Workspace Workflow Additions

Goal: add only the explicitly allowed workspace workflow pieces.

Allowed work:

- ignored directories
- Global Control `ow r` recent workspace command

Not allowed in this track:

- session restore
- include dirs/defines configuration UI
- recent files
- broad workspace UX expansion

Milestones:

- G5.1 Audit current workspace open/recent behavior.
- G5.2 Add ignored-directory model/service support.
- G5.3 Restore `ow r` as a displayed Global Control child command.

## Track 6: Completion / `;cmd` / `;;cmd`

Goal: support user customization and clarify command responsibilities.

Allowed work:

- user templates
- custom abbreviations
- slot mode integration
- clear boundary between `;cmd`, `;;cmd`, and COM Mode

Milestones:

- G6.1 Document current responsibilities and conflict boundaries.
- G6.2 Add user-template storage/query model.
- G6.3 Add custom abbreviation resolution.
- G6.4 Integrate user templates with slot mode.

## Track 7: Fold / Fold Shelf

Goal: make Fold Shelf durable across work sessions and files.

Allowed work:

- persistence
- cross-file restore
- rename shelf item
- search shelf item
- clean shelf item

Milestones:

- G7.1 Define persistence schema and ownership.
- G7.2 Persist/restore same-file shelf items.
- G7.3 Restore shelf items across files.
- G7.4 Add rename/search/clean management actions.

## Track 8: Signal Kernel Graph

Goal: make dense graphs readable.

Allowed work:

- high fanout collapse/grouping
- filtering
- graph search

Milestones:

- G8.1 Add service/report grouping for high fanout.
- G8.2 Render collapsible fanout groups.
- G8.3 Add filter and in-graph search.

## Track 9: Wave Preview

Goal: show waveform sketches for selected RTL context only.

Allowed work:

- selected `always` block sketch
- selected module sketch
- readability polish for the sketch

Not allowed:

- simulator behavior
- timing-accurate verification
- waveform database import
- testbench execution

Milestones:

- G9.1 Selected-`always` entry and scoped report.
- G9.2 Selected-module entry and scoped report.
- G9.3 Sketch readability polish.

## Track 10: State Transition Graph

Goal: show a state transition graph only when the selected variable is a
next-state variable.

Rules:

- Selected `ns` triggers.
- Selected `next_state` triggers.
- Selected `cs` does not trigger.
- Selected `current_state` does not trigger.

Milestones:

- G10.1 Trigger gating and tests for allowed/disallowed names.
- G10.2 Service-owned transition extraction/report.
- G10.3 Graph UI rendering and navigation evidence.

## Track 11: Module Block Diagram

Goal: from a selected module name, show a module-only block diagram rooted at
that module.

Rules:

- show module/interface instance and wrapping relationships only
- do not show signals
- clicking a module block jumps to the module definition

Milestones:

- G11.1 Service report for module containment from selected module.
- G11.2 Module-only block diagram rendering.
- G11.3 Click navigation to module definitions.

## Huge Workspace Status Audit

Huge Workspace is not an active feature expansion track in this goal model.
Only audit and document existing strategy status.

Audit topics:

- current/open/dirty-open priority
- analysis bands and query ordering
- stale request coalescing and expiration
- cancellation
- staged publication
- Activity telemetry
- Release `huge_prj` performance references

Milestones:

- HWA.1 Inventory owner classes, current behavior, and existing verification.
- HWA.2 Run safe verification or compile relevant targets.
- HWA.3 Update docs with confirmed status and open gaps.

## Completion Order

Preferred starting order:

1. G0 Documentation And Goal Reset.
2. G4 COM Mode Framework Completion.
3. G1 Editor Daily Operations Completion.
4. G2 Slot Mode After Template Insertion.
5. G3 Batch RTL Edit Actions.

The remaining tracks may be pulled forward only when the user explicitly asks or
when a milestone naturally depends on them. Do not combine unrelated tracks in
one milestone.
