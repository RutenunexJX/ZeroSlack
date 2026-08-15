# ZeroSlack Current Goal

Product version: `v0.2.1`

## Objective

Publish a verifiable 0.2.1 maintenance release that makes formatter,
effective-literal inspection, and editor key behavior deterministic across
ordinary editing and specialized editor modes.

## Completion criteria

- `VERSION`, generated GUI metadata, current documents, and the release package
  agree on 0.2.1.
- Format Document and Format Selection align complete SystemVerilog structures
  while changing only whitespace and line endings.
- Ordinary Shift+Tab moves backward by unindenting, while slot, multi-cursor,
  and column modes retain their explicit ownership rules.
- ASCII string literals expose their packed equivalent values through the
  existing literal inspector without per-file or per-symbol rules.
- Focused and release regression targets pass, and the fixed-name Windows
  package is rebuilt from the accepted source revision.

## Current status

Implementation and focused regression coverage are complete. The 0.2.1 shared
Release application and the version/document consistency guard pass; this
revision is the accepted fixed-name package baseline.
