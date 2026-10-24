#include "tabmanager.h"
#include "documentrecoveryqueue.h"
#include "editorfileidentity.h"
#include "uidialogs.h"
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QTimer>

QList<WebDocumentPage*> TabManager::webDocuments() const {
    QList<WebDocumentPage*> result;
    const auto groups = splitController ? splitController->groups() : QList<QTabWidget*>{tabWidget};
    for (auto* group : groups) for (int i = 0; group && i < group->count(); ++i)
        if (auto* page = qobject_cast<WebDocumentPage*>(group->widget(i))) result.append(page);
    return result;
}
WebDocumentPage* TabManager::currentWebDocument() const {
    const auto* group = activeTabWidget();
    return group ? qobject_cast<WebDocumentPage*>(group->currentWidget()) : nullptr;
}
bool TabManager::hasCurrentDocument() const { return getCurrentEditor() || currentWebDocument(); }
bool TabManager::importMarkdownDocument(const QString& requested) {
    const QString source = requested.isEmpty() ? QFileDialog::getOpenFileName(activeTabWidget(), QStringLiteral("Import Markdown as Block Document"), QString(), QStringLiteral("Markdown (*.md *.markdown)")) : requested;
    if (source.isEmpty()) return false;
    auto* group = activeTabWidget(); auto* page = new WebDocumentPage(WebDocumentPage::Kind::Blocks, group); QString error;
    if (!page->importMarkdown(source, &error)) { delete page; UiDialogs::warning(group, QStringLiteral("Import Markdown"), error); return false; }
    registerWebDocument(page, group); return true;
}
WebDocumentPage* TabManager::createWebDocument(WebDocumentPage::Kind kind, const QString& text) {
    auto* group = activeTabWidget(); if (!group) return nullptr;
    auto* page = new WebDocumentPage(kind, group); QString error;
    if (!page->setInitialContent(text, &error)) { delete page; UiDialogs::warning(group, QStringLiteral("Open Document"), error); return nullptr; }
    registerWebDocument(page, group); return page;
}
WebDocumentPage* TabManager::openWebDocument(const QString& fileName, QTabWidget* group) {
    if (!group) group = activeTabWidget(); if (!group) return nullptr;
    auto* page = new WebDocumentPage(WebDocumentPage::kindForFile(fileName), group); QString error;
    if (!page->openFile(fileName, &error)) { delete page; UiDialogs::warning(group, QStringLiteral("Open Document"), error); return nullptr; }
    registerWebDocument(page, group); return page;
}
void TabManager::registerWebDocument(WebDocumentPage* page, QTabWidget* group) {
    page->setTargetValidator([this, page](const QString& target, QString* error) {
        for (auto* other : webDocuments()) if (other != page && !other->fileName().isEmpty() && EditorFileIdentity::same(other->fileName(), target)) {
            if (error) *error = QStringLiteral("This file is already open in another document tab."); return false;
        }
        if (sharedDocuments->documentForFile(target)) { if (error) *error = QStringLiteral("This file is already open in a text editor tab."); return false; }
        return true;
    });
    connect(page, &WebDocumentPage::statusChanged, this, [this, page] {
        if (auto* owner = splitController->groupForPage(page)) {
            const int index = owner->indexOf(page); if (index >= 0) { owner->setTabText(index, page->title()); owner->setTabToolTip(index, page->fileName()); }
        }
        if (currentWebDocument() == page) if (auto* host = qobject_cast<QWidget*>(parent())) host->setWindowTitle(page->title() + QStringLiteral(" - ZeroSlack"));
        emit workspaceSessionStateChanged();
    });
    connect(page, &WebDocumentPage::activated, this, [this, page] {
        if (auto* owner = splitController->groupForPage(page)) { splitController->setActiveGroup(owner); owner->setCurrentWidget(page); }
    });
    connect(page, &WebDocumentPage::saveRequested, this, [page](bool saveAs) { page->save(saveAs); });
    connect(page, &WebDocumentPage::fileCommandRequested, this, [this](const QString& command) {
        if (command == QStringLiteral("new")) createNewTab(); else if (command == QStringLiteral("open")) openFileInTab(QString());
    });
    connect(page, &WebDocumentPage::fileSaveFailed, this, &TabManager::fileSaveFailed);
    connect(page, &WebDocumentPage::fileSaved, this, [this, page](const QString& name) { clearWebRecovery(page); applyWorkspaceScope(); emit fileSaved(name); });
    connect(page, &WebDocumentPage::identityChanged, this, [this, page](const QString& previous, const QString& name) {
        emit documentIdentityChanged(page->documentId(), previous, page->documentId(), name);
    });
    connect(page, &WebDocumentPage::recoveryCheckpointRequested, this, [this, page] { checkpointWebRecovery(page); });
    group->addTab(page, page->title()); group->setCurrentWidget(page); splitController->setActiveGroup(group);
    emit webDocumentOpened(page);
    applyWorkspaceScope(); emit workspaceSessionStateChanged();
}
bool TabManager::closeWebDocument(WebDocumentPage* page, bool confirm, bool remember) {
    if (!page || page->busy()) return false;
    if (confirm && !page->confirmClose()) return false;
    auto* group = splitController->groupForPage(page); if (!group) return false;
    const QString name = page->fileName();
    if (remember && !name.isEmpty()) {
        ClosedTabState state; state.webDocument = true; state.webKind = page->documentKind(); state.fileName = name; state.groupIndex = groupIndex(group);
        recentlyClosedTabs.append(state); while (recentlyClosedTabs.size() > 32) recentlyClosedTabs.removeFirst();
    }
    clearWebRecovery(page); group->removeTab(group->indexOf(page)); page->deleteLater();
    if (!closingBatch) splitController->removeEmptyGroups(); emit tabClosed(name); emit workspaceSessionStateChanged(); return true;
}
bool TabManager::confirmWebDocuments(const QList<WebDocumentPage*>& pages) {
    for (auto* page : pages) if (!page->confirmClose()) return false;
    return true;
}
void TabManager::checkpointWebRecovery(WebDocumentPage* page) {
    if (recoveryClosing || !crashRecoveryService || !page || !page->dirty()) return;
    CrashRecoverySnapshotRequest request;
    if (!page->captureRecovery(&request.text, &request.documentRevision)) return;
    QString workspace = workspaceForFile(page->fileName()); if (workspace.isEmpty()) workspace = temporaryRecoveryWorkspace();
    request.document.workspacePath = workspace;
    request.document.originalFilePath = page->fileName();
    if (page->fileName().isEmpty()) request.document.untitledDocumentId = QStringLiteral("web:") + page->documentId()
        + (page->documentKind() == WebDocumentPage::Kind::Markdown ? QStringLiteral(".md") : QStringLiteral(".zblock"));
    request.savedBaselineSha256 = page->savedBaseline();
    if (!recoveryQueue) recoveryQueue = std::make_unique<DocumentRecoveryQueue>(*crashRecoveryService,
        [this](QObject* owner, const auto& snapshot, const auto& result) { completeRecoverySnapshot(owner, snapshot, result); }, this);
    auto& keys = webRecoveryKeys[page];
    const QString recoveryId = crashRecoveryService->recoveryIdForDocument(request.document);
    bool known = false; for (const auto& key : keys) if (crashRecoveryService->recoveryIdForDocument(key) == recoveryId) known = true;
    if (!known) keys.append(request.document);
    recoveryQueue->enqueue(page, std::move(request));
}
void TabManager::clearWebRecovery(WebDocumentPage* page) {
    if (!page || !crashRecoveryService) return;
    if (recoveryQueue) recoveryQueue->cancel(page);
    const auto keys = webRecoveryKeys.take(page);
    for (const auto& key : keys) { const auto result = crashRecoveryService->clearAfterNormalClose(key);
        if (!result.succeeded()) emit crashRecoveryOperationFailed(page->documentId(), result.reason);
    }
}
