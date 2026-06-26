# Issues And Feature Ideas

This file is a running checklist for problems to fix and new features to consider.

## Issues

- [ ] No open implementation issues from the current batch.

## Feature Ideas

- [ ] Add workspace alias rename from the workspace tab right-click menu. The action should be named `Rename`, edit only the workspace alias/display name, leave the disk folder path unchanged, reject empty or duplicate aliases, and update workspace tabs plus recent-workspace metadata.
- [ ] Add editor font zoom with `Ctrl+Shift+mouse wheel`: wheel up increases editor font size, and wheel down decreases editor font size. Keep `Ctrl+mouse wheel` reserved for fast scrolling.
- [ ] Change bracket range quick-select from `Ctrl+left-click` to `Alt+left-click`, so `Ctrl+left-click` inside `[PW-1:0]` and similar ranges can consistently mean jump-to-definition for parameters/macros/symbols.
- [ ] Enhance bracket range Up/Down adjustment for parameter or expression bounds. Examples: `[P_TEST:0]` plus Up should become `[P_TEST+1:0]`; Down should first ensure `P_TEST-1` is not below the right bound, then become `[P_TEST-1:0]`; complex expressions should be parenthesized before arithmetic, e.g. `[P_TEST0*P_TEST1:0]` Down becomes `[(P_TEST0*P_TEST1)-1:0]`.
- [ ] Add a dedicated semantic highlight role for enum values at declaration and use sites, so enum constants such as `IDLE`, `BUSY`, and `DONE` are visually distinct from enum types, enum variables, parameters, macros, and ordinary identifiers. Keep declaration-site ghost values, and use hover/double-click details for richer enum value information instead of always-on inline labels.
- [ ] Add a dedicated semantic highlight role for typedef/type-alias names at use sites, such as `type_def_type test;`, so alias types are visually distinct from ordinary identifiers and not limited to declaration-site `PackageClassType` coloring.
- [ ] Add semantic highlighting for parameter and localparam references at use sites, such as `[P_WIDTH-1:0]`, `assign x = P_DEPTH;`, and parameter expressions. References should use the parameter semantic role consistently instead of falling back to ordinary identifier coloring.
- [ ] Add dedicated semantic highlighting for module ports, so input/output/inout port names are visually distinct from internal signals at declaration and use sites.
- [ ] Add `Ctrl+W` smart selection expansion. When the cursor is inside a symbol, the first press selects the symbol; the next press expands to the containing member/hierarchical expression such as `test_struct.test_member`; then to the current parenthesized expression content such as `test_data == 1`; then to the next outer parenthesized expression content such as `(test_s.test_m == 1) & (test_data == 1)`, without including the outermost parentheses. When the cursor starts on an operator or number inside parentheses, the first press should select the parenthesized expression content.
- [ ] Add selected-symbol occurrence navigation in the current file. When a symbol is selected, `Ctrl+E` should jump to the next occurrence/call site in the file, and `Ctrl+Q` should jump to the previous occurrence/call site. Navigation should wrap around from the end to the beginning, and from the beginning to the end.
- [ ] Add safe rename for the selected symbol with `Ctrl+R`. Show a rename dialog, then rename the definition and all resolved call/reference sites. If the selected symbol has no definition, prompt whether to create one; accepting opens a definition input dialog that supports `;cmd` and `;;cmd` expansion, analyzes the generated declaration, and inserts it at a valid location before the first use and after required type definitions such as inserting `st_e cs;` after the `st_e` enum type definition. Rejecting cancels the rename. If the new name conflicts with an existing definition, show `Force rename` and `Rename conflicting definition first`; the second option must rename the conflicting definition before the requested rename, and that intermediate rename must not introduce another definition conflict.

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
