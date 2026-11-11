#include "tabmanager.h"
#include "documentrecoveryqueue.h"
#include "editorfileidentity.h"
#include "uidialogs.h"
#include "roundedicons.h"
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
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
bool TabManager::hasCurrentDocument() const { return getCurrentEditor() || currentWebDocument() || currentPdfDocument(); }
bool TabManager::importMarkdownDocument(const QString& requested) {
    const QString source = requested.isEmpty() ? QFileDialog::getOpenFileName(activeTabWidget(), QStringLiteral("Import Markdown as Block Document"), QString(), QStringLiteral("Markdown (*.md *.markdown)")) : requested;
    if (source.isEmpty()) return false;
    auto* group = activeTabWidget(); auto* page = new WebDocumentPage(WebDocumentPage::Kind::Blocks, group); QString error;
    if (!page->importMarkdown(source, &error)) { delete page; UiDialogs::warning(group, QStringLiteral("Import Markdown"), error); return false; }
    registerWebDocument(page, group); return true;
}
WebDocumentPage* TabManager::createWebDocument(WebDocumentPage::Kind kind, const QString& text, const QString& region) {
    auto* group = region.isEmpty() ? activeTabWidget() : splitController->regionGroup(region);
    if (!group) return nullptr;
    auto* page = new WebDocumentPage(kind, group); QString error;
    if (!page->setInitialContent(text, &error)) { delete page; UiDialogs::warning(group, QStringLiteral("Open Document"), error); return nullptr; }
    registerWebDocument(page, group); return page;
}
WebDocumentPage* TabManager::createWebDocumentFile(WebDocumentPage::Kind kind, const QString& requested, const QString& region, QString* error) {
    if (requested.isEmpty()) return createWebDocument(kind, {}, region);
    auto* group = region.isEmpty() ? activeTabWidget() : splitController->regionGroup(region);
    if (!group) return nullptr;
    QString target = requested;
    const auto extension = kind == WebDocumentPage::Kind::Markdown ? QStringLiteral(".md") : QStringLiteral(".zblock");
    if (QFileInfo(target).suffix().isEmpty()) target += extension;
    QString why;
    const auto fail = [&]() -> WebDocumentPage* { if (error) *error = why; else UiDialogs::warning(group, QStringLiteral("Create Document"), why); return nullptr; };
    if (QFileInfo::exists(target)) { why = QStringLiteral("A file already exists at this path. Choose another name."); return fail(); }
    QPointer<WebDocumentPage> page = new WebDocumentPage(kind, group);
    if (!page->setInitialContent(QString(), &why)) { delete page; return fail(); }
    registerWebDocument(page, group); splitController->activatePage(page);
    QEventLoop waiting; QTimer timeout; timeout.setSingleShot(true); timeout.setInterval(30000);
    connect(page, &WebDocumentPage::statusChanged, &waiting, [&] { if (!page || page->ready()) waiting.quit(); });
    connect(page, &QObject::destroyed, &waiting, &QEventLoop::quit); connect(&timeout, &QTimer::timeout, &waiting, &QEventLoop::quit);
    timeout.start(); if (!page->ready()) waiting.exec();
    if (!page) return nullptr;
    if (!page->ready()) why = QStringLiteral("The document editor did not finish loading. No file was created.");
    else if (page->documentSession()->createFile(target, &why)) return page;
    closeWebDocument(page, false, false); return fail();
}
bool TabManager::openFileInRegion(const QString& path, const QString& region) {
    auto* group = splitController->regionGroup(region); if (!group) return false;
    splitController->setActiveGroup(group);
    if (!openFileInTab(path)) return false;
    auto* current = activeTabWidget() ? activeTabWidget()->currentWidget() : nullptr;
    return current && (splitController->groupForPage(current) == group ? splitController->activatePage(current) : splitController->movePage(current, group));
}
WebDocumentPage* TabManager::openWebDocument(const QString& fileName, QTabWidget* group, const QString& viewId, const QString& documentId, QString* failure) {
    if (!group) group = activeTabWidget(); if (!group) return nullptr;
    for (auto* existing : webDocuments()) if (EditorFileIdentity::same(existing->fileName(), fileName)) {
        if (existing->busy()) return nullptr;
        auto* page = new WebDocumentPage(existing->documentSession(), group, viewId); registerWebDocument(page, group); return page;
    }
    auto* page = new WebDocumentPage(WebDocumentPage::kindForFile(fileName), group, {{}, {}, documentId}, viewId); QString error;
    if (!page->openFile(fileName, &error)) { delete page; if (failure) *failure = error; else UiDialogs::warning(group, QStringLiteral("Open Document"), error); return nullptr; }
    registerWebDocument(page, group); return page;
}
void TabManager::registerWebDocument(WebDocumentPage* page, QTabWidget* group) {
    splitController->registerPage(page, {}, page->documentKind() == WebDocumentPage::Kind::Markdown ? QStringLiteral("markdown") : QStringLiteral("blocks"));
    connect(page, &WebDocumentPage::viewStateChanged, this, &TabManager::workspaceSessionStateChanged);
    auto* session = page->documentSession().get();
    if (!observedWebDocuments.contains(session)) {
        observedWebDocuments.insert(session);
        connect(session, &QObject::destroyed, this, [this, session] { observedWebDocuments.remove(session); webRecoveryKeys.remove(session); });
        session->setTargetValidator([this, session](const QString& target, QString* error) {
            for (auto* other : webDocuments()) if (other->documentSession().get() != session && !other->fileName().isEmpty() && EditorFileIdentity::same(other->fileName(), target)) {
                if (error) *error = QStringLiteral("This file is already open in another document session."); return false;
            }
            if (sharedDocuments->documentForFile(target)) { if (error) *error = QStringLiteral("This file is already open in a text editor tab."); return false; }
            return true;
        });
        connect(session, &WebDocumentSession::fileSaveFailed, this, &TabManager::fileSaveFailed);
        connect(session, &WebDocumentSession::fileSaved, this, [this, session](const QString& name) {
            clearWebRecovery(session->views().value(0)); applyWorkspaceScope(); emit fileSaved(name);
        });
        connect(session, &WebDocumentSession::identityChanged, this, [this, session](const QString& previous, const QString& name) {
            emit documentIdentityChanged(session->documentId(), previous, session->documentId(), name);
        });
        connect(session, &WebDocumentSession::recoveryCheckpointRequested, this, [this, session] { checkpointWebRecovery(session->views().value(0)); });
    }
    connect(page, &WebDocumentPage::statusChanged, this, [this, page] {
        if (auto* owner = splitController->groupForPage(page)) {
            const int index = owner->indexOf(page); if (index >= 0) { owner->setTabText(index, page->title()); owner->setTabToolTip(index, page->fileName().isEmpty() ? page->title() : page->fileName()); }
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
    group->addTab(page, RoundedIcons::icon(page->documentKind() == WebDocumentPage::Kind::Markdown ? RoundedIcons::File : RoundedIcons::Grid), page->title()); group->setCurrentWidget(page); splitController->setActiveGroup(group);
    group->setTabToolTip(group->indexOf(page),page->fileName().isEmpty() ? page->title() : page->fileName());
    emit webDocumentOpened(page);
    applyWorkspaceScope(); emit workspaceSessionStateChanged();
}
bool TabManager::closeWebDocument(WebDocumentPage* page, bool confirm, bool remember) {
    if (!page || page->busy()) return false;
    if (confirm && !page->confirmClose()) return false;
    auto* group = splitController->groupForPage(page); if (!group) return false;
    const QString name = page->fileName();
    if (remember && !name.isEmpty()) {
        ClosedTabState state; state.webDocument = true; state.webKind = page->documentKind(); state.fileName = name; rememberClosedGroup(state, group);
        recentlyClosedTabs.append(state); while (recentlyClosedTabs.size() > 32) recentlyClosedTabs.removeFirst();
    }
    if (page->documentSession()->views().size() == 1) clearWebRecovery(page);
    splitController->unregisterPage(page);
    group->removeTab(group->indexOf(page)); page->releaseView(); page->deleteLater();
    if (!closingBatch) splitController->removeEmptyGroups(); emit tabClosed(name); emit workspaceSessionStateChanged(); return true;
}
bool TabManager::confirmWebDocuments(const QList<WebDocumentPage*>& pages) {
    QHash<WebDocumentSession*, int> counts;
    for (auto* page : pages) if (page) ++counts[page->documentSession().get()];
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() == it.key()->views().size() && !it.key()->confirmClose()) return false;
    return true;
}
void TabManager::checkpointWebRecovery(WebDocumentPage* page) {
    if (recoveryClosing || !crashRecoveryService || !page || !page->dirty()) return;
    if (page->documentSession()->views().value(0) != page) return;
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
    auto* session = page->documentSession().get();
    auto& keys = webRecoveryKeys[session];
    const QString recoveryId = crashRecoveryService->recoveryIdForDocument(request.document);
    bool known = false; for (const auto& key : keys) if (crashRecoveryService->recoveryIdForDocument(key) == recoveryId) known = true;
    if (!known) keys.append(request.document);
    recoveryQueue->enqueue(session, std::move(request));
}
void TabManager::clearWebRecovery(WebDocumentPage* page) {
    if (!page || !crashRecoveryService) return;
    auto* session = page->documentSession().get();
    if (recoveryQueue) recoveryQueue->cancel(session);
    const auto keys = webRecoveryKeys.take(session);
    for (const auto& key : keys) { const auto result = crashRecoveryService->clearAfterNormalClose(key);
        if (!result.succeeded()) emit crashRecoveryOperationFailed(page->documentId(), result.reason);
    }
}
