# ZeroSlack Current Goal

Product version: `v0.4.1`

## Objective

Deliver a verifiable 0.4.1 keyboard-reliability patch that restores continuous
`Alt+Up` / `Alt+Down` line movement, keeps the caret attached to each repeated
downward move, and lets an active column selection expand horizontally with
`Shift+Left` / `Shift+Right`.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.4.1.
- Auto-repeat key presses execute one line move per event until the document
  boundary, with the original column and logical selection preserved.
- Active column selections accept Shift-only horizontal adjustment while
  retaining `Shift+Alt+Arrow` as the explicit entry gesture.
- Focused controller and GUI regressions cover both behaviors, followed by the
  complete configured test suite.

## Current status

Implementation and verification are complete. A clean configure and full Debug
build pass, and all 86 configured tests pass, including the focused line-move
and column-selection regressions. This revision is the `v0.4.1` release source.
