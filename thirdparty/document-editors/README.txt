ZeroSlack document editors

Markdown (.md and .markdown) uses Muya from MarkText 0.23.0. The left sidebar's
Documents menu creates Markdown documents. Open, Save, Save As and close
confirmation use the ordinary ZeroSlack document workflow. PDF uses PDF.js.
The retired .zblock format is not opened or converted. Original files,
attachments and recovery data remain untouched. Embedded blocks in code
documents are a separate feature and remain supported.

All editor JavaScript, CSS, fonts, formulas and supported diagrams load locally.
Microsoft Edge WebView2 Evergreen Runtime (x64) must be installed on Windows.
If the runtime is absent, install it from Microsoft's official distribution:
https://developer.microsoft.com/microsoft-edge/webview2/
Restart ZeroSlack after installation. ZeroSlack does not download or install a
runtime automatically and does not require an internet connection for editing.

For Markdown, copy the document and its relative image
folders, including .zeroslack-assets. Save As copies referenced local images
inside the source document folder to the chosen target folder; unavailable or
escaping references produce an error. Remote resources are not fetched.

Build from source with a local Node executable and Python 3.12 or later:
python scripts/build-document-editors.py --node <absolute-node.exe>
  --work-directory <build-cache> --output-directory <new-resource-directory>
The command verifies inputs.json archive hashes, installs the frozen lockfiles
with lifecycle scripts disabled, builds the Markdown editor, collects the actual bundle
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
