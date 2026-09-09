# Non-editor typography

The UI uses a proportional sans-serif face (Noto Sans SC, Microsoft YaHei UI, Noto Sans, Segoe UI, then the platform fallback). Code, diffs and editor-owned popups retain their editor fonts. Sizes below are logical pixels and follow display scaling.

| Role | Size | Weight | Use |
| --- | --- | --- | --- |
| Page title | 18 | 600 | Settings category title |
| Panel title | 14 | 600 | Dock titles, context headings, graph node titles |
| Body | 13 | 400 | File names, controls, values, menu items, Activity |
| Section | 12 | 600 | Table headers and form/navigation group labels |
| Metadata | 12 | 400 | Descriptions, scope summaries, diagnostic counts |
| Badge | 11 | 600 | Compact unread counters |

Selected tabs use weight 600; unselected tabs and ordinary values remain 400. Supporting text uses the existing secondary text color rather than the disabled color. Theme palettes are unchanged. Do not add arbitrary letter spacing to file names or Chinese text.

Use `UiTypography::apply` for explicit widget roles and the shared InsightVisualStyle helpers for themed labels. Reapplying a role is idempotent: metadata must not get progressively smaller on theme changes. Avoid global font QSS rules that override editor-owned popup and code fonts.

Spacing follows a 4 px rhythm: 8 px between related controls, 12–16 px within forms, and 16–20 px between major groups. Tree rows reserve 24 px plus vertical padding; tabs use 8 px vertical and 14 px horizontal padding. Activity paragraphs have 4 px bottom spacing. Navigation explanations occupy a full row above their actions to remain readable in a narrow sidebar.

Validate Light/Dark transitions, proportional glyph metrics, editor font isolation, narrow sidebars, and 125%/150%/200% display scaling. Offscreen Windows previews load installed system UI fonts because the offscreen Qt platform does not provide the native font database; these font files are not bundled or redistributed.
