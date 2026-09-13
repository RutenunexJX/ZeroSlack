# ZeroSlack Current Goal

Product version: `v0.28.1`

The Context sidebar supports a registry-owned whole-sidebar toggle through Ctrl+2 and the View menu.
Showing an empty sidebar opens the first rail provider's default view and restores a hidden rail.
The four Context phases now include placement dimensions, native floating windows, multiple document-bound
views and a collapsible sidebar stack with drag between surfaces. Real desktop drag interaction and native
multi-monitor/restart frame measurements remain unverified. See docs/sidebar-stack-review.md.

Maintain a focused SystemVerilog editor with Tree-sitter structural editing and Slang semantic authority.
Detachable Context views use native floating windows with workspace geometry memory and inactive opacity.
Temporary source editors retain their overlay; v6 Context state reads v5 with only the old active section expanded.
Older builds cannot restore v6 Context state.
Worker-verified idle trivia refresh preserves semantic availability for unsaved comments and whitespace;
structural edits retain the existing save requirement and action safety checks.
Compact layouts preserve readable tabs and reachable Context actions without increasing dock widths.
Workspace Hub, provider-based Context Workspace, live insights, Wave simulation integration and the read-only
AI CLI are implemented. Native documents and external application data remain with their owners.

Future usability work should address one concrete workflow at a time. No new whole-window redesign is scheduled
by this document. Current capabilities are in [README](README.md), interaction in [the manual](用户手册.md),
and ownership/cancellation constraints in [architecture](ARCHITECTURE.md).
