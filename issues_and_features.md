# Issues And Feature Ideas

This file is a running checklist for problems to fix and new features to consider.

## Issues

- [ ] No open implementation issues from the current batch.

## Feature Ideas

- [ ] No open feature ideas from the current batch.

## Discussion Needed

- [ ] 

## Done

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
