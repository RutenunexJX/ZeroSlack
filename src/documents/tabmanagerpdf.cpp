#include "tabmanager.h"
#include "editorfileidentity.h"
#include "roundedicons.h"
#include "uidialogs.h"
#include "uicontrols.h"
#include <QDir>
#include <QFileInfo>

QList<PdfDocumentPage*> TabManager::pdfDocuments() const {
    QList<PdfDocumentPage*> result;
    const auto groups = splitController ? splitController->groups() : QList<QTabWidget*>{tabWidget};
    for (auto* group : groups) for (int i = 0; group && i < group->count(); ++i)
        if (auto* page = qobject_cast<PdfDocumentPage*>(group->widget(i))) result.append(page);
    return result;
}
PdfDocumentPage* TabManager::currentPdfDocument() const {
    auto* group = activeTabWidget(); return group ? qobject_cast<PdfDocumentPage*>(group->currentWidget()) : nullptr;
}
PdfDocumentPage* TabManager::openPdfDocument(const QString& fileName, QTabWidget* group, const QString& viewId, QString* failure) {
    if (!group) group = activeTabWidget(); if (!group) return nullptr;
    auto* page = new PdfDocumentPage(group, viewId); QString error;
    if (!page->openFile(fileName, &error)) { delete page; if (failure) *failure = error; else UiDialogs::warning(group, QStringLiteral("Open PDF"), error); return nullptr; }
    connect(page, &PdfDocumentPage::activated, this, [this, page] {
        if (auto* owner = splitController->groupForPage(page)) { splitController->setActiveGroup(owner); owner->setCurrentWidget(page); }
    });
    connect(page, &PdfDocumentPage::viewStateChanged, this, &TabManager::workspaceSessionStateChanged);
    connect(page, &PdfDocumentPage::fileCommandRequested, this, [this](const QString& command) {
        if (command == QStringLiteral("new")) createNewTab(); else if (command == QStringLiteral("open")) openFileInTab(QString());
    });
    connect(page, &PdfDocumentPage::statusChanged, this, [this, page] {
        if (auto* owner = splitController->groupForPage(page)) {
            const int index = owner->indexOf(page);
            if (index >= 0) {
                owner->setTabText(index, page->title());
                UiControls::TabPresentation state;
                state.documentId = QStringLiteral("pdf:") + EditorFileIdentity::lookupKey(page->fileName());
                state.viewId = page->viewIdentity(); state.title = page->title();
                state.path = QDir::toNativeSeparators(page->fileName()); state.readOnly = true;
                UiControls::setTabPresentation(owner, page, state);
            }
        }
    });
    splitController->registerPage(page, {}, QStringLiteral("pdf"));
    group->addTab(page, RoundedIcons::icon(RoundedIcons::File), page->title()); group->setCurrentWidget(page); splitController->setActiveGroup(group);
    UiControls::TabPresentation state;
    state.documentId = QStringLiteral("pdf:") + EditorFileIdentity::lookupKey(page->fileName());
    state.viewId = page->viewIdentity(); state.title = page->title();
    state.path = QDir::toNativeSeparators(page->fileName()); state.readOnly = true;
    UiControls::setTabPresentation(group, page, state);
    applyWorkspaceScope(); emit workspaceSessionStateChanged(); return page;
}
bool TabManager::closePdfDocument(PdfDocumentPage* page, bool remember) {
    auto* group = page ? splitController->groupForPage(page) : nullptr; if (!group) return false;
    const QString name = page->fileName();
    if (remember && !name.isEmpty()) {
        ClosedTabState state; state.pdfDocument = true; state.fileName = name; rememberClosedGroup(state, group); state.pdfState = page->viewState();
        recentlyClosedTabs.append(state); while (recentlyClosedTabs.size() > 32) recentlyClosedTabs.removeFirst();
    }
    splitController->unregisterPage(page);
    group->removeTab(group->indexOf(page)); repairContextPage(); page->releaseView(); page->deleteLater();
    if (!closingBatch) splitController->removeEmptyGroups(); emit tabClosed(name); emit workspaceSessionStateChanged(); return true;
}
