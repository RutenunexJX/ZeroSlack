# Issues And Feature Ideas

This file is a running checklist for problems to fix and new features to consider.

## Issues

- [ ] Signal Kernel Graph hover preview flickers and can move outside the graph
      viewport. The panel already uses `EditorHoverPopup`, so stabilize the
      graph-side hover lifecycle instead of introducing a second popup class:
      avoid rebuilding the popup on every hover move over the same node, clamp
      placement to the graph view/dock viewport, avoid hover scale changes that
      perturb hit testing, and delay close briefly on hover leave.
- [ ] Signal Kernel Graph node double-click navigation is currently broken.
      Double-clicking a node should close any hover popup and navigate to the
      node's evidence or declaration link. If item-level double-click handling
      remains unreliable under `QGraphicsView::ScrollHandDrag`, add a graph-view
      level fallback that resolves the node under the cursor and routes the same
      navigation action.
- [ ] Global or package-defined enum values are not semantically highlighted at
      use sites, while locally defined enum values are highlighted. The likely
      cause is `SemanticDecorationService::decorationsForDocument()` building
      usage-highlight candidates only from `getSymbolRecords(query.fileName)`;
      enum values defined in package/global/include files are therefore absent
      from the current document's usage candidate set. Fix by deriving visible
      usage candidates from the current file plus imported/package/global
      semantic records, while keeping local symbols higher priority to avoid
      false highlighting of unrelated same-name identifiers.

## Feature Ideas

- [ ] Redesign Signal Kernel Graph node and module presentation. Node line 1
      should show the signal/access-path name, node line 2 should show only type
      and width, while relationship/evidence/detail text moves to hover. Draw a
      module frame for every module represented in the graph, including the
      kernel module, and place all input, kernel, and output nodes that belong to
      the same module inside that module frame with the module name as the frame
      title.
- [ ] Split Signal Kernel Graph inputs into dependency lanes instead of a flat
      input list. Use structure as the primary distinction and color only as an
      aid: Data inputs for RHS data dependencies, Control inputs for assignment
      guards such as `if` / `case` / ternary conditions, and Timing inputs for
      clock/reset/event-control dependencies. Module frames answer ownership;
      Data/Control/Timing lanes answer how each signal affects the kernel.
      Conditions should eventually be bound to the specific assignment target
      they control, so unrelated module-level condition reads do not appear as
      inputs for every signal in the module.

## Discussion Needed

- [ ] 

## Done

- [x] Regression: after double-click selecting a symbol, Ctrl+click jump-to-definition can select all text between the call site and the definition in large design contexts. Ctrl+click navigation now consumes the matching mouse release after the jump so the editor cannot extend the previous selection anchor at the definition site.
- [x] Column selection zero-width rendering is wrong. Zero-width rectangular selections now render as one vertical caret per selected line, including virtual-column positions on short lines, without broad text-selection highlighting.
- [x] Add `Ctrl+D` duplicate selection/line editor action. With a selection, it duplicates the selected text and selects the new copy; without a selection, it duplicates the current logical line below while preserving the cursor column, including final lines without trailing newlines.
- [x] Refine column selection to the requested Notepad++-like interaction model. The normal editor caret is the anchor, `Shift+Alt+click` sets or updates the endpoint, `Shift+Alt+Arrow` adjusts the endpoint, plain click or `Esc` exits, and text input, Backspace, Delete, virtual-column padding, rectangular Copy/Cut/Paste operate on every selected line.
- [x] Add `Alt+Up` / `Alt+Down` line-block move. It moves the current logical line or every logical line touched by the selection, preserves cursor or selection range, treats a selection ending at next-line column 0 as excluding that line, and reports that `Alt+Up/Down` is disabled while column selection is active.
- [x] Ctrl+S save shortcut does not work in manual testing, even though the Save action still declares `Ctrl+S` in `mainwindow.ui`. Registered the Save action as an application-level shortcut on the main window and verified current-editor save behavior.
- [x] After Ctrl+click jumping to a package definition, mouse side-button back navigation behaves abnormally and requires two presses. Navigation history now records the pre-jump source once before cross-file line jumps.
- [x] After Ctrl+click jumping to an include file, mouse side-button back navigation fails. Include Ctrl+click now routes through the navigation coordinator so the source location is pushed to back history.
- [x] After double-click selecting a symbol, Ctrl+click navigation selects all text from the jump origin to the definition location. Ctrl+click navigation now clears the active selection at the clicked position before jumping.
- [x] If the mouse is already hovering a symbol, pressing Ctrl does not immediately show the definition hover; it only appears after a small mouse move. The editor now remembers the last mouse position and refreshes hover there when Ctrl is pressed.
- [x] Double-click numeric base-conversion hover for values such as `'haaaa` disappears after a tiny mouse move. Numeric hover pinning now accepts double-click selections that cover the literal digits even when the quote/base prefix is outside the selection.
- [x] Ctrl+click jump-to-definition can still show a duplicate definition hover at the definition site. Ctrl hover now suppresses definition preview popups when the resolved target is the current editor location.
- [x] When multiple workspaces are open, switching the active workspace should hide editor tabs that belong to other workspaces, so each workspace tab shows only its own files instead of mixing all open editor tabs together. Scratch tabs and files outside all opened workspaces are scoped as global tabs and stay visible in every workspace.
- [x] Add a visible close action for workspace tabs/workspaces. Workspace tabs now show close buttons, closing a workspace prompts for unsaved files in that workspace before removing its editor tabs, switches the active workspace predictably, tears down the previous watcher, and leaves scratch/external tabs available.
- [x] Add workspace alias rename from the workspace tab right-click menu. The action is named `Rename`, edits only the workspace alias/display name, preserves the disk folder path, rejects empty or duplicate aliases, and updates workspace tabs plus recent-workspace metadata.
- [x] Add editor font zoom with `Ctrl+Shift+mouse wheel`: wheel up increases editor font size, wheel down decreases editor font size, and `Ctrl+mouse wheel` remains a fast-scroll gesture instead of changing font size.
- [x] Change bracket range quick-select from `Ctrl+left-click` to `Alt+left-click`, so `Ctrl+left-click` inside `[PW-1:0]` and similar ranges remains available for jump-to-definition while `Alt+left-click` selects the range body.
- [x] Enhance bracket range Up/Down adjustment for parameter or expression bounds. Parameter bounds now become `P_TEST+1` / `P_TEST-1`, numeric left-bound Down is clamped so it does not move below a numeric right bound, and complex expressions are parenthesized before arithmetic such as `[(P_TEST0*P_TEST1)-1:0]`.
- [x] Add a dedicated semantic highlight role for enum values at declaration and use sites, so enum constants such as `IDLE`, `BUSY`, and `DONE` are visually distinct from enum types, enum variables, parameters, macros, and ordinary identifiers while richer enum details remain in hover/double-click flows.
- [x] Add a dedicated semantic highlight role for typedef/type-alias names at use sites, such as `type_def_type test;`, so alias types are visually distinct from ordinary identifiers and not limited to declaration-site `PackageClassType` coloring.
- [x] Add semantic highlighting for parameter and localparam references at use sites, such as `[P_WIDTH-1:0]`, `assign x = P_DEPTH;`, and parameter expressions. References now reuse the parameter semantic role instead of falling back to ordinary identifier coloring.
- [x] Add dedicated semantic highlighting for module ports, so input/output/inout port names are visually distinct from internal signals at declaration and use sites.
- [x] Add `Ctrl+W` smart selection expansion. It now expands symbol -> member/hierarchical expression -> current parenthesized expression content -> next outer parenthesized expression content, and starts from operator/number positions inside parentheses by selecting the parenthesized expression content.
- [x] Add selected-symbol occurrence navigation in the current file. When a symbol is selected, `Ctrl+E` jumps to the next occurrence/call site and `Ctrl+Q` jumps to the previous occurrence/call site, preserving selection and wrapping at file boundaries.
- [x] Add current-file `Ctrl+R` safe rename MVP. It prompts for a valid identifier, renames all current-file word-boundary occurrences in one edit block, preserves similarly prefixed names, and offers `Force rename` / `Rename conflicting definition first` choices for current-file name conflicts.
- [x] Extend `Ctrl+R` safe rename beyond the current-file MVP. The app-level path now resolves the selected symbol, builds a cross-file edit plan for definition and resolved reference occurrences from semantic file contents/open editors, applies edits through `TabManager` so files stay tracked as open/dirty documents, detects new-name definition conflicts with `Force rename` and `Rename conflicting definition first`, rejects intermediate conflict names, and supports missing-definition creation through raw declarations or `;cmd` / `;;cmd` template expansion inserted before first use and after required type definitions such as enum/typedef records.
