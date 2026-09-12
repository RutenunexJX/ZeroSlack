# ZeroSlack Current Goal

Product version: `v0.25.11`

Maintain a focused SystemVerilog editor with Tree-sitter structural editing and Slang semantic authority.
Context placement now separates surface, persistence and binding while retaining existing behavior and v3 state compatibility.
Worker-verified idle trivia refresh preserves semantic availability for unsaved comments and whitespace;
structural edits retain the existing save requirement and action safety checks.
Compact layouts preserve readable tabs and reachable Context actions without increasing dock widths.
Workspace Hub, provider-based Context Workspace, live insights, Wave simulation integration and the read-only
AI CLI are implemented. Native documents and external application data remain with their owners.

Future usability work should address one concrete workflow at a time. No new whole-window redesign is scheduled
by this document. Current capabilities are in [README](README.md), interaction in [the manual](用户手册.md),
and ownership/cancellation constraints in [architecture](ARCHITECTURE.md).
