# Non-editor typography

The UI uses a proportional sans-serif face (Noto Sans, Segoe UI, Noto Sans SC, Microsoft YaHei UI, then the platform fallback). Code, diffs and editor-owned popups retain their editor fonts. Sizes below are logical pixels and follow display scaling.

| Role | Size | Weight | Use |
| --- | --- | --- | --- |
| Page title | 18 | 600 | Settings category title |
| Panel title | 14 | 600 | Dock titles and context headings |
| Body | 13 | 400 | File names, controls, values, menu items, Activity |
| Section | 12 | 600 | Table headers and form/navigation group labels |
| Metadata | 12 | 400 | Descriptions, scope summaries, diagnostic counts |
| Badge | 11 | 400 | Compact unread counters |

Selected tabs use weight 600; unselected tabs and ordinary values remain 400. Supporting text uses the existing secondary text color rather than the disabled color. Light/Dark use softer primary text and distinct secondary text; Catppuccin provides Latte, Frappe, Macchiato and Mocha palettes. Do not add arbitrary letter spacing to file names or Chinese text.

Use `UiTypography::apply` for explicit widget roles and the shared InsightVisualStyle helpers for themed labels. Graph font helpers preserve their supplied graph font and are separate from these UI roles. Reapplying a role is idempotent: metadata must not get progressively smaller on theme changes. Avoid global font QSS rules that override editor-owned popup and code fonts.

Spacing follows a 4 px rhythm: 8 px between related controls, 12–16 px within forms, and 16–20 px between major groups. Tree rows reserve 24 px plus vertical padding; tabs use 8 px vertical and 14 px horizontal padding. Activity paragraphs have 4 px bottom spacing. Navigation explanations occupy a full row above their actions to remain readable in a narrow sidebar.

Validate Light/Dark transitions, proportional glyph metrics, editor font isolation, narrow sidebars, and 125%/150%/200% display scaling. Offscreen Windows previews load installed system UI fonts because the offscreen Qt platform does not provide the native font database; these font files are not bundled or redistributed.


The fixed title row exposes a right-click menu on the file path: Copy full path and
Reveal in Explorer. Display width does not constrain the copied absolute path.
Tab close glyphs are centered within the original hit area and capped at the text scale.
Windows chrome retains native overlapped-window capabilities and exposes HTMAXBUTTON
for the system snap menu while drawing the themed title row.
