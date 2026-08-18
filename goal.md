# ZeroSlack Current Goal

Product version: `v0.4.0`

## Objective

Deliver a verifiable 0.4.0 contextual-editing release covering the F24 command
layer, scoped `Ctrl+Space` completion, semantic rename, predictable lexical and
structural movement, explicit parenthesis wrapping, and bracket-suffix
formatter alignment.

## Completion criteria

- `VERSION`, generated GUI metadata, and current documents agree on 0.4.0.
- F24 direct, fuzzy-search, cancellation, rapid-input, and repeat semantics are
  deterministic and covered at the event level.
- `Ctrl+Space` honors explicit semantic filters and resolved scope/type context.
- `Ctrl+R` renames only the resolved semantic identity and preserves review for
  structural or cross-file plans.
- Lexical and structural navigation share stable source models and preserve
  multi-cursor selection anchors.
- Parenthesis and formatter behavior preserve all non-whitespace source tokens
  and remain stable on a second formatting pass.

## Current status

Implementation and verification are complete. A clean configure and full Debug
build pass, and all 86 configured tests pass, including command, completion,
rename, lexical/structural movement, multi-cursor, formatter, GUI, workspace,
semantic, performance, architecture, and policy guards. This verified revision
is the `v0.4.0` release source.
