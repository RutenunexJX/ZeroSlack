# ZeroSlack Current Goal

Product version: `v0.3.2`

## Objective

Deliver a verifiable 0.3.2 editor reliability patch that keeps column-mode
carets on the exact insertion boundary selected by the user and prevents a
held `Alt+Up` / `Alt+Down` key from moving logical lines repeatedly.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.3.2.
- ASCII and Tab insertion columns are independent of accumulated font-metric
  rounding; real-text caret pixels resolve from the exact `QTextCursor`.
- Mouse positions in the final half-cell resolve to the real EOL boundary,
  including fractional display scaling and the column immediately after `;`.
- `Alt+Up` / `Alt+Down` executes once for the initial press and ignores only
  auto-repeat key events; separate key presses still move one row each.
- Existing real/virtual column editing, Tab, Unicode, clipboard, selection,
  and line-operation behavior remains covered without fixture-specific rules.

## Current status

Implementation and focused verification are complete. GUI regression passes
at 1.0 and 1.25 scale factors, the independent line-operation test passes, and
the version-documentation guard passes. The full Debug suite passes 85 of 86
tests; the only failure is the pre-existing visible-Wave performance budget in
`editor_incremental_test` (measured p95 8.0 ms and max 19.8 ms against 6/12 ms
budgets). Its functional and incremental assertions pass, and this patch does
not change the Wave path or relax its thresholds. This task does not publish or
package the revision.
