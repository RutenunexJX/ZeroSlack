ZeroSlack document editors

Markdown (.md and .markdown) uses Muya from MarkText 0.23.0. Block documents
(.zblock) use BlockSuite 0.22.4. File > New Markdown Document and New Block
Document create documents. Open, Save, Save As and close confirmation use the
ordinary ZeroSlack document workflow. The File menu is inside the Project menu.
Import Markdown as Block Document and Export Block Document as Markdown create
independent documents; they do not replace the source. Conversion notices state
which information cannot be represented by the target format.

All editor JavaScript, CSS, fonts, formulas and supported diagrams load locally.
Microsoft Edge WebView2 Evergreen Runtime (x64) must be installed on Windows.
If the runtime is absent, install it from Microsoft's official distribution:
https://developer.microsoft.com/microsoft-edge/webview2/
Restart ZeroSlack after installation. ZeroSlack does not download or install a
runtime automatically and does not require an internet connection for editing.

Block documents contain native Yjs document state and image attachments. Copy
the .zblock file to move a document. For Markdown, also copy its relative image
folders, including .zeroslack-assets. Save As copies referenced local images
inside the source document folder to the chosen target folder; unavailable or
escaping references produce an error. Remote resources are not fetched.

Build from source with a local Node executable and Python 3.12 or later:
python scripts/build-document-editors.py --node <absolute-node.exe>
  --work-directory <build-cache> --output-directory <new-resource-directory>
The command verifies inputs.json archive hashes, installs the frozen lockfiles
with lifecycle scripts disabled, builds both editors, collects the actual bundle
dependency licenses, and writes manifest.json. Retain the archive directory and
pnpm store for offline rebuilds. --skip-install only uses an existing workspace
with unchanged lockfiles. No global Node or package-manager configuration is used.

Configure the native build with:
  -DZEROSLACK_WEBVIEW2_SDK_DIR=<build-cache>/vendor/webview2
  -DZEROSLACK_DOCUMENT_EDITORS_DIR=<new-resource-directory>
Build the demo target. Package with scripts/package-release.ps1 and
-DocumentEditorDirectory <new-resource-directory>. Packaging verifies every
manifest entry and includes the loader, resources, licenses and source archives.

See resources/document-editors/NOTICE.txt, licenses/ and sources/ for the pinned
upstream revisions, complete upstream sources, modifications and build inputs.
