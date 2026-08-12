#include "actionregistry.h"
#include "applicationthememanager.h"
#include "documentmodel.h"
#include "editorcoordinator.h"
#include "editorlocation.h"
#include "editorsemanticcontextservice.h"
#include "editorsplitcontroller.h"
#include "externaldocumentsynccontroller.h"
#include "filecommandcoordinator.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "temporaryeditordrawer.h"
#include "temporaryeditordrawercontroller.h"
#include "unsaveddocumentmanager.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
}

void pumpEvents(int milliseconds = 0)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    } while (timer.elapsed() < milliseconds);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

bool waitUntil(const std::function<bool()>& predicate,
               int timeoutMilliseconds = 500)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds)
        pumpEvents(2);
    return predicate();
}

bool writeTextFile(const QString& fileName, const QString& text)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Text
                   | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

QString readTextFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString documentText(const QString& prefix, int lineCount)
{
    QString text;
    for (int line = 1; line <= lineCount; ++line) {
        text += QStringLiteral("%1 line %2 payload_abcdefghijklmnopqrstuvwxyz\n")
                    .arg(prefix)
                    .arg(line, 3, 10, QLatin1Char('0'));
    }
    return text;
}

int positionFor(QTextDocument* document, int line, int column)
{
    if (!document)
        return -1;
    const QTextBlock block = document->findBlockByNumber(line - 1);
    if (!block.isValid())
        return -1;
    return block.position()
        + std::clamp(column - 1, 0, std::max(0, block.length() - 1));
}

struct SplitLayoutSnapshot {
    QList<QTabWidget*> groups;
    QList<QSplitter*> splitters;
    QList<QList<int>> sizes;
    QList<Qt::Orientation> orientations;
};

SplitLayoutSnapshot captureSplitLayout(
    QWidget* editorRegion,
    EditorSplitController* controller)
{
    SplitLayoutSnapshot snapshot;
    if (controller)
        snapshot.groups = controller->groups();
    if (!editorRegion)
        return snapshot;
    snapshot.splitters = editorRegion->findChildren<QSplitter*>();
    for (QSplitter* splitter : snapshot.splitters) {
        snapshot.sizes.append(splitter->sizes());
        snapshot.orientations.append(splitter->orientation());
    }
    return snapshot;
}

bool splitLayoutMatches(const SplitLayoutSnapshot& expected,
                        QWidget* editorRegion,
                        EditorSplitController* controller)
{
    if (!editorRegion || !controller
        || controller->groups() != expected.groups) {
        return false;
    }
    const QList<QSplitter*> current =
        editorRegion->findChildren<QSplitter*>();
    if (current != expected.splitters
        || current.size() != expected.sizes.size()) {
        return false;
    }
    for (int index = 0; index < current.size(); ++index) {
        if (current.at(index)->sizes() != expected.sizes.at(index)
            || current.at(index)->orientation()
                   != expected.orientations.at(index)) {
            return false;
        }
    }
    return true;
}

bool rectInside(const QRect& outer, const QRect& inner)
{
    return inner.isValid()
        && outer.contains(inner.topLeft())
        && outer.contains(inner.bottomRight());
}

QAction* findMenuAction(QMenu* menu, const QString& actionId)
{
    if (!menu)
        return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->property("actionId").toString() == actionId)
            return action;
        if (QMenu* subMenu = action->menu()) {
            if (QAction* found = findMenuAction(subMenu, actionId))
                return found;
        }
    }
    return nullptr;
}

void focusOutsideDrawer(QLineEdit* focusSink,
                        TemporaryEditorDrawer* drawer)
{
    if (focusSink) {
        focusSink->show();
        focusSink->raise();
        focusSink->setFocus(Qt::OtherFocusReason);
    }
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(drawer, &leave);
    pumpEvents(10);
}

void exerciseLocationModel(const QString& fileName)
{
    EditorSelectionRange range{12, 3, 14, 9};
    EditorLocation location;
    location.documentId = QStringLiteral("stable-document-id");
    location.filePath = fileName;
    location.line = 12;
    location.column = 3;
    location.selection = range;
    location.symbolKey = QStringLiteral("work.top.target");
    location.sourceLinkId = QStringLiteral("definition:target");

    const OpenTarget target = location;
    expect("EditorLocation retains stable document identity and file path",
           target.isValid()
               && target.hasStableDocumentId()
               && target.documentKey()
                      == QStringLiteral("stable-document-id")
               && target.filePath == fileName);
    expect("EditorLocation retains one-based location and selection metadata",
           target.line == 12
               && target.column == 3
               && target.selection.has_value()
               && *target.selection == range
               && target.selection->isValid());
    expect("EditorLocation retains symbol and source-link metadata",
           target.symbolKey == QStringLiteral("work.top.target")
               && target.sourceLinkId
                      == QStringLiteral("definition:target")
               && target.displayText().contains(target.symbolKey));

    EditorLocation sameDocument = location;
    sameDocument.line = 90;
    sameDocument.selection.reset();
    expect("locations distinguish positions while preserving document identity",
           location.refersToSameDocument(sameDocument)
               && !location.equivalentTo(sameDocument));

    EditorLocation invalidRange = location;
    invalidRange.selection = EditorSelectionRange{5, 1, 4, 1};
    expect("invalid reversed selections are rejected",
           !invalidRange.isValid());
}

void exerciseDrawerGeometry(TemporaryEditorDrawer* drawer,
                            QWidget* editorRegion,
                            QLineEdit* focusSink)
{
    using Edge = TemporaryEditorDrawer::Edge;
    using State = TemporaryEditorDrawer::State;
    const QList<Edge> edges = {
        Edge::Left,
        Edge::Right,
        Edge::Top,
        Edge::Bottom,
    };

    bool allHandlesAreSmall = true;
    bool allHandlesAreTheOnlyVisibleSurface = true;
    for (Edge edge : edges) {
        drawer->stow(edge);
        pumpEvents();
        const QRect handle = drawer->handleRect();
        const bool vertical = edge == Edge::Left || edge == Edge::Right;
        allHandlesAreSmall = allHandlesAreSmall
            && drawer->state() == State::EdgeStowed
            && drawer->edge() == edge
            && drawer->isHandleVisible()
            && drawer->handleWidget()->isVisible()
            && rectInside(editorRegion->rect(), handle)
            && (vertical
                    ? handle.width() <= 30 && handle.height() <= 220
                    : handle.height() <= 30 && handle.width() <= 220);
        allHandlesAreTheOnlyVisibleSurface =
            allHandlesAreTheOnlyVisibleSurface
            && drawer->contentRect().isEmpty()
            && handle.size() == drawer->size();
    }
    expect("all four edges stow to a bounded small handle",
           allHandlesAreSmall);
    expect("stowed state exposes only the handle, not an edge-wide sensor",
           allHandlesAreTheOnlyVisibleSurface);

    drawer->stow(Edge::Right);
    const QPointF handleLocal(2.0, 2.0);
    const QPointF handleGlobal(
        drawer->handleWidget()->mapToGlobal(QPoint(2, 2)));
    QEnterEvent enter(handleLocal, handleLocal, handleGlobal);
    QApplication::sendEvent(drawer->handleWidget(), &enter);
    pumpEvents();
    expect("hovering the small handle expands the drawer",
           drawer->isExpandedFromHandle()
               && !drawer->isHandleVisible()
               && !drawer->contentRect().isEmpty());

    drawer->setAutoCollapseDelayMs(40);
    focusOutsideDrawer(focusSink, drawer);
    expect("leaving an unpinned drawer does not collapse synchronously",
           drawer->isExpandedFromHandle());
    expect("leaving an unpinned drawer collapses after the delay",
           waitUntil([drawer]() {
               return drawer->isHandleVisible();
           }, 500));

    drawer->stow(Edge::Left);
    drawer->pinButton()->click();
    focusOutsideDrawer(focusSink, drawer);
    pumpEvents(120);
    expect("Pin disables delayed auto-collapse",
           drawer->pinned() && drawer->isExpandedFromHandle());
    drawer->pinButton()->click();
    focusOutsideDrawer(focusSink, drawer);
    expect("unpinning restores delayed auto-collapse",
           waitUntil([drawer]() {
               return drawer->isHandleVisible();
           }, 500));

    drawer->stow(Edge::Top);
    drawer->expandFromHandle();
    drawer->setExternalInteractionActive(true);
    focusOutsideDrawer(focusSink, drawer);
    pumpEvents(120);
    expect("popup or completion interaction guard prevents collapse",
           drawer->interactionActive()
               && drawer->isExpandedFromHandle());
    drawer->setExternalInteractionActive(false);
    focusOutsideDrawer(focusSink, drawer);
    expect("clearing the external interaction guard permits collapse",
           waitUntil([drawer]() {
               return drawer->isHandleVisible();
           }, 500));

    bool allPreviewsMatch = true;
    for (Edge edge : edges) {
        drawer->showDockPreview(edge);
        pumpEvents();
        const QRect preview = drawer->dockPreviewRect();
        allPreviewsMatch = allPreviewsMatch
            && drawer->dockPreviewVisible()
            && drawer->dockPreviewEdge() == edge
            && rectInside(editorRegion->rect(), preview)
            && preview.width() > 0
            && preview.height() > 0;
        if (edge == Edge::Left || edge == Edge::Right) {
            allPreviewsMatch = allPreviewsMatch
                && preview.height() == editorRegion->height()
                && preview.width() <= (editorRegion->width() + 1) / 2;
        } else {
            allPreviewsMatch = allPreviewsMatch
                && preview.width() == editorRegion->width()
                && preview.height() <= (editorRegion->height() + 1) / 2;
        }
        drawer->clearDockPreview();
        allPreviewsMatch = allPreviewsMatch
            && !drawer->dockPreviewVisible()
            && drawer->dockPreviewRect().isEmpty();
    }
    expect("all four docking destinations expose bounded previews",
           allPreviewsMatch);
}
} // namespace

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);

    QTemporaryDir temporaryDirectory;
    expect("temporary test directory is available",
           temporaryDirectory.isValid());
    if (!temporaryDirectory.isValid())
        return 1;

    const QString firstFile = temporaryDirectory.filePath(
        QStringLiteral("first_document.sv"));
    const QString secondFile = temporaryDirectory.filePath(
        QStringLiteral("second_document.sv"));
    const QString firstText = documentText(QStringLiteral("first"), 240);
    const QString secondText = documentText(QStringLiteral("second"), 180);
    expect("complete document fixtures are written",
           writeTextFile(firstFile, firstText)
               && writeTextFile(secondFile, secondText));
    exerciseLocationModel(firstFile);

    QWidget editorRegion;
    editorRegion.setObjectName(QStringLiteral("editorRegion"));
    auto* editorLayout = new QVBoxLayout(&editorRegion);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    auto* tabs = new QTabWidget(&editorRegion);
    editorLayout->addWidget(tabs);

    TabManager tabManager(tabs);
    tabManager.enableSplitLayout(&editorRegion);
    EditorCoordinator editorCoordinator(&tabManager);
    editorCoordinator.connectSignals();
    FileCommandCoordinator fileCommands(
        &tabManager, nullptr, &editorRegion);
    editorRegion.resize(1000, 720);
    editorRegion.show();
    editorRegion.activateWindow();
    expect("first complete document opens in the main editor",
           tabManager.openFileInTab(firstFile));
    expect("real split baseline is created",
           tabManager.splitCurrentView(EditorSplitDirection::Right));
    pumpEvents(30);

    MyCodeEditor* mainEditor = tabManager.getCurrentEditor();
    SharedDocument* firstDocument =
        tabManager.sharedDocumentForEditor(mainEditor);
    expect("main editor has a stable shared document",
           mainEditor && firstDocument
               && !firstDocument->documentId().isEmpty()
               && mainEditor->document()
                      == firstDocument->textDocument());
    const int splitCountBefore = tabManager.splitCount();
    const int tabEditorCountBefore = tabManager.editorCount();
    const SplitLayoutSnapshot splitBefore = captureSplitLayout(
        &editorRegion, tabManager.editorSplitController());
    expect("baseline contains two persistent split groups",
           splitCountBefore == 2
               && splitBefore.groups.size() == 2
               && splitBefore.splitters.size() == 1);

    mainEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QTextCursor mainCursor(firstDocument->textDocument());
    mainCursor.setPosition(positionFor(
        firstDocument->textDocument(), 2, 5));
    mainEditor->setTextCursor(mainCursor);
    QScrollBar* mainScroll = mainEditor->verticalScrollBar();
    mainScroll->setValue(std::min(4, mainScroll->maximum()));
    const int mainPositionBeforeDrawer = mainEditor->textCursor().position();
    const int mainAnchorBeforeDrawer = mainEditor->textCursor().anchor();
    const int mainScrollBeforeDrawer = mainScroll->value();

    auto drawerSettings = std::make_unique<QSettings>(
        temporaryDirectory.filePath(QStringLiteral("drawer.ini")),
        QSettings::IniFormat);
    drawerSettings->clear();
    TemporaryEditorDrawerController controller(
        &tabManager,
        &editorRegion,
        std::move(drawerSettings),
        &editorRegion);
    TemporaryEditorDrawer* drawer = controller.drawer();

    EditorLocation firstLocation;
    firstLocation.documentId = firstDocument->documentId();
    firstLocation.filePath = firstFile;
    firstLocation.line = 20;
    firstLocation.column = 2;
    firstLocation.selection = EditorSelectionRange{20, 2, 20, 8};
    firstLocation.symbolKey = QStringLiteral("first.selected_symbol");
    firstLocation.sourceLinkId = QStringLiteral("reference:first:20");
    expect("controller opens a valid temporary target",
           controller.openLocation(firstLocation));
    pumpEvents(30);

    MyCodeEditor* drawerEditor = controller.editor();
    SharedDocument* drawerDocument =
        tabManager.sharedDocumentForEditor(drawerEditor);
    expect("drawer is an editor-region child rather than a system window",
           drawer
               && drawer->parentWidget() == &editorRegion
               && editorRegion.isAncestorOf(drawer)
               && !drawer->isWindow()
               && qobject_cast<QDockWidget*>(drawer) == nullptr);
    expect("temporary view uses the same QTextDocument and DocumentModel authority",
           drawerEditor
               && drawerDocument == firstDocument
               && drawerEditor->document()
                      == mainEditor->document()
               && tabManager.isAuxiliaryView(drawerEditor)
               && tabManager.auxiliaryViews()
                      == QList<MyCodeEditor*>{drawerEditor});
    expect("drawer opens the complete document rather than a selection copy",
           drawerEditor->toPlainText() == firstText
               && drawerEditor->document()->blockCount()
                      == firstDocument->textDocument()->blockCount());
    const QTextCursor initialDrawerCursor = drawerEditor->textCursor();
    expect("selection metadata is used only for initial drawer positioning",
           initialDrawerCursor.anchor()
                   == positionFor(drawerEditor->document(), 20, 2)
               && initialDrawerCursor.position()
                      == positionFor(drawerEditor->document(), 20, 8)
               && initialDrawerCursor.hasSelection());
    expect("opening the drawer preserves main cursor, selection, and scroll",
           mainEditor->textCursor().position()
                   == mainPositionBeforeDrawer
               && mainEditor->textCursor().anchor()
                      == mainAnchorBeforeDrawer
               && mainScroll->value() == mainScrollBeforeDrawer);
    expect("drawer title exposes the file and symbol target",
           drawer->targetTitle().contains(
               QFileInfo(firstFile).fileName())
               && drawer->targetTitle().contains(
                   firstLocation.symbolKey));
    expect("drawer title bar provides navigation, search, Pin, and Close controls",
           drawer->backButton()
               && drawer->forwardButton()
               && drawer->searchField()
               && drawer->pinButton()
               && drawer->closeButton()
               && !drawer->backButton()->toolTip().isEmpty()
               && !drawer->forwardButton()->toolTip().isEmpty()
               && !drawer->pinButton()->toolTip().isEmpty()
               && !drawer->closeButton()->toolTip().isEmpty());

    const QString externallyReloadedText =
        firstText + QStringLiteral("// external reload\n");
    ExternalDocumentSyncController* externalSync =
        tabManager.externalDocumentSyncController();
    const bool externalSourceChanged =
        writeTextFile(firstFile, externallyReloadedText);
    const ExternalDocumentSyncResult externalReload =
        externalSync
        ? externalSync->processFileChange(firstFile)
        : ExternalDocumentSyncResult();
    pumpEvents();
    const DocumentSnapshot mainDocumentSnapshot =
        tabManager.getDocumentModel()
            ->documentForEditor(mainEditor);
    const DocumentSnapshot drawerDocumentSnapshot =
        tabManager.getDocumentModel()
            ->documentForEditor(drawerEditor);
    expect("external refresh uses the existing SharedDocument and DocumentModel for main and drawer views",
           externalSourceChanged
               && externalReload.outcome
                      == ExternalDocumentSyncOutcome::Reloaded
               && drawerEditor->document()
                      == mainEditor->document()
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && drawerEditor->toPlainText()
                      == externallyReloadedText
               && mainEditor->toPlainText()
                      == externallyReloadedText
               && mainDocumentSnapshot.documentId
                      == drawerDocumentSnapshot.documentId
               && mainDocumentSnapshot.documentId
                      == firstDocument->documentId()
               && !firstDocument->dirty());

    drawerEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QScrollBar* drawerScroll = drawerEditor->verticalScrollBar();
    const int drawerTargetScroll = std::min(80, drawerScroll->maximum());
    drawerScroll->setValue(drawerTargetScroll);
    pumpEvents();
    expect("main and drawer retain independent scroll state",
           drawerScroll->maximum() > 0
               && drawerScroll->value() == drawerTargetScroll
               && mainScroll->value() == mainScrollBeforeDrawer);

    QTextCursor drawerEdit = drawerEditor->textCursor();
    drawerEdit.insertText(QStringLiteral("DRAWER_EDIT"));
    drawerEditor->setTextCursor(drawerEdit);
    pumpEvents();
    expect("drawer edits synchronize immediately into every main view",
           firstDocument->textDocument()->toPlainText().contains(
               QStringLiteral("DRAWER_EDIT"))
               && mainEditor->toPlainText()
                      == drawerEditor->toPlainText()
               && firstDocument->dirty());
    expect("shared edits do not merge main and drawer cursor state",
           mainEditor->textCursor().position()
                   == mainPositionBeforeDrawer
               && mainEditor->textCursor().anchor()
                      == mainAnchorBeforeDrawer
               && !mainEditor->textCursor().hasSelection()
               && mainScroll->value() == mainScrollBeforeDrawer);
    expect("saving from the drawer uses the shared document save path",
           controller.saveCurrent()
               && readTextFile(firstFile).contains(
                   QStringLiteral("DRAWER_EDIT"))
               && !firstDocument->dirty());

    EditorLocation sameDocumentLocation = firstLocation;
    sameDocumentLocation.line = 120;
    sameDocumentLocation.column = 7;
    sameDocumentLocation.selection.reset();
    sameDocumentLocation.symbolKey = QStringLiteral("first.later_symbol");
    expect("new target in the same file reuses one auxiliary editor",
           controller.openLocation(sameDocumentLocation));
    pumpEvents();
    expect("same-file different location keeps the document and moves only drawer cursor",
           controller.editor() == drawerEditor
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && drawerEditor->textCursor().blockNumber() == 119
               && mainEditor->textCursor().position()
                      == mainPositionBeforeDrawer
               && controller.historyCount() == 2);

    EditorLocation secondLocation;
    secondLocation.filePath = secondFile;
    secondLocation.line = 30;
    secondLocation.column = 4;
    secondLocation.symbolKey = QStringLiteral("second.target");
    expect("different-file target rebinds the temporary view",
           controller.openLocation(secondLocation));
    pumpEvents();
    SharedDocument* secondDocument =
        tabManager.sharedDocumentForEditor(drawerEditor);
    expect("different-file target still opens one complete shared document",
           controller.editor() == drawerEditor
               && secondDocument
               && secondDocument != firstDocument
               && drawerEditor->document()
                      == secondDocument->textDocument()
               && drawerEditor->toPlainText() == secondText
               && controller.currentLocation()
                      .hasStableDocumentId()
               && controller.historyCount() == 3
               && controller.canGoBack()
               && !controller.canGoForward());
    expect("target replacement creates no temporary tabs or split groups",
           tabManager.editorCount() == tabEditorCountBefore
               && tabManager.splitCount() == splitCountBefore
               && tabManager.auxiliaryViews().size() == 1
               && splitLayoutMatches(
                   splitBefore,
                   &editorRegion,
                   tabManager.editorSplitController()));

    const QString drawerViewId =
        drawerEditor->property("editorViewId").toString();
    mainEditor->setFocus(Qt::OtherFocusReason);
    pumpEvents();
    expect("focus-aware target resolves the focused main editor",
           QApplication::focusWidget()
               && tabManager.editorActionTarget() == mainEditor);
    expect("an explicit registered auxiliary view id overrides popup focus",
           !drawerViewId.isEmpty()
               && tabManager.editorActionTarget(drawerViewId)
                      == drawerEditor
               && tabManager.editorActionTarget(
                      QStringLiteral("stale-editor-view"))
                      == nullptr);

    QString routedAction;
    QString routedViewId;
    MyCodeEditor* routedEditor = nullptr;
    bool routedSucceeded = false;
    editorCoordinator.setRegisteredActionRequestHandler(
        [&](const QString& actionId,
            const QVariantMap& parameters) {
            routedAction = actionId;
            routedViewId = parameters
                .value(QStringLiteral("editorViewId"))
                .toString();
            routedEditor = tabManager.editorActionTarget(
                routedViewId);
            const bool standardEditorAction =
                actionId == QStringLiteral("edit.undo")
                || actionId == QStringLiteral("edit.redo")
                || actionId == QStringLiteral("edit.cut")
                || actionId == QStringLiteral("edit.copy")
                || actionId == QStringLiteral("edit.paste")
                || actionId == QStringLiteral("select.all");
            routedSucceeded = routedEditor != nullptr;
            if (!routedSucceeded || !standardEditorAction)
                return;
            if (actionId == QStringLiteral("edit.undo")) {
                routedSucceeded = routedEditor->document()
                    ->isUndoAvailable();
                if (routedSucceeded)
                    routedEditor->undo();
            } else if (actionId
                       == QStringLiteral("edit.redo")) {
                routedSucceeded = routedEditor->document()
                    ->isRedoAvailable();
                if (routedSucceeded)
                    routedEditor->redo();
            } else if (actionId
                       == QStringLiteral("edit.copy")) {
                routedEditor->copy();
            } else if (actionId
                       == QStringLiteral("edit.cut")) {
                routedSucceeded = !routedEditor->isReadOnly();
                if (routedSucceeded)
                    routedEditor->cut();
            } else if (actionId
                       == QStringLiteral("edit.paste")) {
                routedSucceeded = !routedEditor->isReadOnly()
                    && QApplication::clipboard()
                    && !QApplication::clipboard()
                            ->text().isEmpty();
                if (routedSucceeded)
                    routedEditor->paste();
            } else if (actionId
                       == QStringLiteral("select.all")) {
                routedSucceeded = routedEditor->document()
                    ->characterCount() > 1;
                if (routedSucceeded)
                    routedEditor->selectAll();
            }
        });
    const auto requestDrawerEditorAction =
        [&](const QString& actionId) {
            routedAction.clear();
            routedViewId.clear();
            routedEditor = nullptr;
            routedSucceeded = false;
            bool handled = false;
            emit drawerEditor->registeredActionRequested(
                actionId, {}, &handled);
            pumpEvents();
            return handled
                && routedAction == actionId
                && routedViewId == drawerViewId
                && routedEditor == drawerEditor
                && routedSucceeded;
        };

    QTextCursor undoCursor(drawerEditor->document());
    undoCursor.setPosition(0);
    undoCursor.insertText(QStringLiteral("UNDO_FROM_DRAWER"));
    expect("drawer Undo action carries the auxiliary view identity and targets it",
           requestDrawerEditorAction(
               QStringLiteral("edit.undo"))
               && drawerEditor->toPlainText() == secondText
               && mainEditor->document()
                      == firstDocument->textDocument());

    QTextCursor copyCursor(drawerEditor->document());
    copyCursor.setPosition(0);
    copyCursor.setPosition(
        QStringLiteral("second").size(),
        QTextCursor::KeepAnchor);
    drawerEditor->setTextCursor(copyCursor);
    QApplication::clipboard()->clear();
    expect("drawer Copy action uses the drawer selection",
           requestDrawerEditorAction(
               QStringLiteral("edit.copy"))
               && QApplication::clipboard()->text()
                      == QStringLiteral("second")
               && drawerEditor->toPlainText() == secondText);

    QTextCursor emptySelection(drawerEditor->document());
    emptySelection.setPosition(0);
    drawerEditor->setTextCursor(emptySelection);
    const QString beforeEmptySelectionActions =
        drawerEditor->toPlainText();
    const QTextBlock firstClipboardBlock =
        drawerEditor->document()->firstBlock();
    const QString expectedLineClipboardText =
        firstClipboardBlock.text() + QLatin1Char('\n');
    const bool lineCopySucceeded =
        requestDrawerEditorAction(
            QStringLiteral("edit.copy"));
    const bool copiedCompleteLine =
        QApplication::clipboard()->text()
            == expectedLineClipboardText
        && drawerEditor->toPlainText()
               == beforeEmptySelectionActions;
    const bool lineCutSucceeded =
        requestDrawerEditorAction(
            QStringLiteral("edit.cut"));
    expect("focus routing preserves no-selection whole-line Copy and Cut semantics",
           lineCopySucceeded
               && copiedCompleteLine
               && lineCutSucceeded
               && QApplication::clipboard()->text()
                      == expectedLineClipboardText
               && drawerEditor->toPlainText()
                      == beforeEmptySelectionActions.mid(
                          expectedLineClipboardText.size()));

    QTextCursor selectedCutCursor(drawerEditor->document());
    selectedCutCursor.setPosition(0);
    selectedCutCursor.setPosition(
        QStringLiteral("second").size(),
        QTextCursor::KeepAnchor);
    drawerEditor->setTextCursor(selectedCutCursor);
    expect("drawer Cut action edits only the drawer shared document",
           requestDrawerEditorAction(
               QStringLiteral("edit.cut"))
               && !drawerEditor->toPlainText().startsWith(
                   QStringLiteral("second"))
               && mainEditor->document()
                      == firstDocument->textDocument());

    QTextCursor pasteCursor(drawerEditor->document());
    pasteCursor.setPosition(0);
    drawerEditor->setTextCursor(pasteCursor);
    QApplication::clipboard()->setText(
        QStringLiteral("PASTED_FROM_DRAWER"));
    expect("drawer Paste action uses the drawer cursor",
           requestDrawerEditorAction(
               QStringLiteral("edit.paste"))
               && drawerEditor->toPlainText().startsWith(
                   QStringLiteral("PASTED_FROM_DRAWER")));
    expect("drawer Select All action selects the auxiliary document",
           requestDrawerEditorAction(
               QStringLiteral("select.all"))
               && drawerEditor->textCursor().hasSelection()
               && drawerEditor->textCursor().selectionStart() == 0
               && drawerEditor->textCursor().selectionEnd()
                      == drawerEditor->document()->characterCount() - 1);

    drawer->setAutoCollapseDelayMs(70);
    drawer->stow(TemporaryEditorDrawer::Edge::Right);
    drawer->expandFromHandle();
    drawerEditor->setFocus(Qt::PopupFocusReason);
    pumpEvents();
    auto* interactionMenu = new QMenu(drawerEditor);
    const EditorSemanticContext popupContext =
        drawerEditor->editorSemanticContextForPosition(
            drawerEditor->textCursor().position(), true);
    emit drawerEditor->sourceSymbolContextMenuRequested(
        interactionMenu, popupContext);
    QAction* popupAction = findMenuAction(
        interactionMenu,
        QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen));
    interactionMenu->popup(
        drawerEditor->mapToGlobal(QPoint(20, 20)));
    pumpEvents(20);
    QEvent drawerLeave(QEvent::Leave);
    QApplication::sendEvent(drawer, &drawerLeave);
    pumpEvents(drawer->autoCollapseDelayMs() + 40);
    const bool popupProtectedExpansion =
        interactionMenu->isVisible()
        && interactionMenu->parentWidget() == drawerEditor
        && drawer->interactionActive()
        && drawer->isExpandedFromHandle();
    if (popupAction && popupAction->isEnabled())
        popupAction->trigger();
    pumpEvents(20);
    expect("a real drawer-owned context menu protects expansion while its action executes",
           popupProtectedExpansion
               && popupAction
               && popupAction->isEnabled()
               && interactionMenu->isVisible()
               && drawer->isExpandedFromHandle()
               && routedAction
                      == QString::fromLatin1(
                          ActionIds::ViewTemporaryEditorOpen)
               && routedEditor == drawerEditor
               && routedSucceeded);
    interactionMenu->hide();
    mainEditor->setFocus(Qt::OtherFocusReason);
    QApplication::sendEvent(drawer, &drawerLeave);
    pumpEvents();
    expect("closing the context menu restores delayed edge collapse",
           waitUntil([drawer]() {
               return drawer->isHandleVisible();
           }, drawer->autoCollapseDelayMs() + 400));
    drawer->setState(TemporaryEditorDrawer::State::Floating);
    pumpEvents();

    QTextCursor saveCursor(drawerEditor->document());
    saveCursor.movePosition(QTextCursor::End);
    saveCursor.insertText(
        QStringLiteral("// SAVE_FROM_FOCUSED_DRAWER\n"));
    drawerEditor->setTextCursor(saveCursor);
    const QString expectedSavedText =
        drawerEditor->toPlainText();
    const QString firstDiskBeforeFocusedSave =
        readTextFile(firstFile);
    bool saveShortcutTriggered = false;
    bool saveShortcutSucceeded = false;
    QAction saveShortcut(&editorRegion);
    saveShortcut.setShortcut(QKeySequence::Save);
    saveShortcut.setShortcutContext(Qt::ApplicationShortcut);
    editorRegion.addAction(&saveShortcut);
    QObject::connect(
        &saveShortcut,
        &QAction::triggered,
        &editorRegion,
        [&]() {
            saveShortcutTriggered = true;
            saveShortcutSucceeded =
                fileCommands.saveEditor(QString(), false);
        });
    drawerEditor->setFocus(Qt::ShortcutFocusReason);
    pumpEvents();
    const bool drawerOwnsFocus =
        tabManager.editorActionTarget() == drawerEditor;
    QTest::keyClick(
        drawerEditor,
        Qt::Key_S,
        Qt::ControlModifier);
    pumpEvents(20);
    expect("Ctrl+S resolves the focused auxiliary editor and saves its shared document",
           drawerOwnsFocus
               && saveShortcutTriggered
               && saveShortcutSucceeded
               && readTextFile(secondFile)
                      == expectedSavedText
               && readTextFile(firstFile)
                      == firstDiskBeforeFocusedSave
               && !secondDocument->dirty());
    editorRegion.removeAction(&saveShortcut);

    drawer->backButton()->click();
    pumpEvents();
    expect("Back restores the previous document and independent location",
           controller.currentLocation().equivalentTo(
               sameDocumentLocation)
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && drawerEditor->textCursor().blockNumber() == 119
               && controller.canGoBack()
               && controller.canGoForward());
    drawer->backButton()->click();
    pumpEvents();
    expect("Back traverses the drawer-only history without moving main view",
           controller.currentLocation().equivalentTo(firstLocation)
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && mainEditor->textCursor().position()
                      == mainPositionBeforeDrawer);
    drawer->forwardButton()->click();
    pumpEvents();
    expect("Forward restores the captured same-document view state",
           controller.currentLocation().equivalentTo(
               sameDocumentLocation)
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && drawerEditor->textCursor().blockNumber() == 119);

    int searchProviderCalls = 0;
    controller.setSearchProvider(
        [&searchProviderCalls, &secondLocation](const QString& query)
            -> EditorSearchCandidates {
            ++searchProviderCalls;
            if (query != QStringLiteral("second.target"))
                return {};
            EditorSearchCandidate candidate;
            candidate.location = secondLocation;
            candidate.title = QStringLiteral("second.target");
            candidate.type = EditorSearchCandidateType::Symbol;
            candidate.disambiguation = QStringLiteral("second document");
            return {candidate};
        });
    drawer->searchField()->setText(QStringLiteral("second.target"));
    QTest::keyClick(drawer->searchField(), Qt::Key_Return);
    pumpEvents();
    expect("title search routes through the shared EditorLocation provider",
           searchProviderCalls == 1
               && controller.currentLocation()
                      .refersToSameDocument(secondLocation)
               && drawerEditor->document()
                      == secondDocument->textDocument()
               && controller.historyCount() == 3);
    drawer->backButton()->click();
    pumpEvents();
    expect("search target participates in the same drawer history",
           controller.currentLocation().equivalentTo(
               sameDocumentLocation)
               && drawerEditor->document()
                      == firstDocument->textDocument());

    QSignalSpy reboundEditSpy(
        tabManager.getDocumentModel(),
        &DocumentModel::documentEdited);
    bool reboundRoundTripSucceeded = true;
    for (int cycle = 0; cycle < 2; ++cycle) {
        reboundRoundTripSucceeded =
            controller.openLocation(secondLocation)
            && controller.openLocation(firstLocation)
            && reboundRoundTripSucceeded;
    }
    const int editedBeforeReboundChange =
        reboundEditSpy.count();
    drawerEditor->insertPlainText(
        QStringLiteral("// rebound once\n"));
    pumpEvents();
    expect("A-B-A rebinds keep one DocumentModel subscription set and process one edit once",
           reboundRoundTripSucceeded
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && reboundEditSpy.count()
                      == editedBeforeReboundChange + 1
               && firstDocument->textDocument()
                      ->toPlainText()
                      .contains(QStringLiteral("rebound once")));

    const QString previousFirstDocumentId =
        firstDocument->documentId();
    const QString saveAsFile =
        temporaryDirectory.filePath(
            QStringLiteral("first_saved_as.sv"));
    const bool drawerSaveAsSucceeded =
        tabManager.saveEditorView(
            drawerEditor,
            true,
            saveAsFile);
    const EditorLocation savedAsLocation =
        controller.currentLocation();
    const bool switchedAfterSaveAs =
        controller.openLocation(secondLocation);
    controller.goBack();
    const EditorLocation backAfterSaveAs =
        controller.currentLocation();
    controller.goForward();
    const EditorLocation forwardAfterSaveAs =
        controller.currentLocation();
    controller.goBack();
    expect("Save As atomically rewrites current history identity and title before Back-Forward navigation",
           drawerSaveAsSucceeded
               && QFileInfo::exists(saveAsFile)
               && firstDocument->documentId() == saveAsFile
               && firstDocument->fileName() == saveAsFile
               && savedAsLocation.documentId == saveAsFile
               && savedAsLocation.filePath == saveAsFile
               && savedAsLocation.documentId
                      != previousFirstDocumentId
               && switchedAfterSaveAs
               && backAfterSaveAs.documentId == saveAsFile
               && backAfterSaveAs.filePath == saveAsFile
               && forwardAfterSaveAs
                      .refersToSameDocument(secondLocation)
               && controller.currentLocation().documentId
                      == saveAsFile
               && drawerEditor->document()
                      == firstDocument->textDocument()
               && drawer->targetTitle().contains(
                   QFileInfo(saveAsFile).fileName()));

    auto* focusSink = new QLineEdit(&editorRegion);
    focusSink->setObjectName(QStringLiteral("drawerTestFocusSink"));
    focusSink->setGeometry(4, 4, 120, 24);
    exerciseDrawerGeometry(drawer, &editorRegion, focusSink);
    expect("stowing, hovering, pinning, and previewing do not alter the split layout",
           tabManager.splitCount() == splitCountBefore
               && splitLayoutMatches(
                   splitBefore,
                   &editorRegion,
                   tabManager.editorSplitController()));

    drawer->setState(TemporaryEditorDrawer::State::Floating);
    pumpEvents();
    const QColor lightDrawerSurface =
        drawer->palette().color(QPalette::Window);
    drawer->stow(TemporaryEditorDrawer::Edge::Right);
    pumpEvents();
    const QImage lightHandleImage = drawer->grab().toImage();
    drawer->showDockPreview(TemporaryEditorDrawer::Edge::Left);
    pumpEvents();
    QWidget* dockPreview = editorRegion.findChild<QWidget*>(
        QStringLiteral("temporaryEditorDrawerDockPreview"));
    const QImage lightPreviewImage = dockPreview
        ? dockPreview->grab().toImage()
        : QImage();
    drawer->clearDockPreview();
    drawer->setState(TemporaryEditorDrawer::State::Floating);
    pumpEvents();
    MyCodeEditor* editorBeforeTheme = controller.editor();
    QTextDocument* documentBeforeTheme = editorBeforeTheme->document();
    const int cursorBeforeTheme = editorBeforeTheme->textCursor().position();
    const int anchorBeforeTheme = editorBeforeTheme->textCursor().anchor();
    const int drawerScrollBeforeTheme =
        editorBeforeTheme->verticalScrollBar()->value();
    const int historyBeforeTheme = controller.historyCount();
    const int historyIndexBeforeTheme = controller.historyIndex();

    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    drawer->refreshTheme();
    pumpEvents(20);
    const QColor darkDrawerSurface =
        drawer->palette().color(QPalette::Window);
    drawer->stow(TemporaryEditorDrawer::Edge::Right);
    pumpEvents();
    const QImage darkHandleImage = drawer->grab().toImage();
    drawer->showDockPreview(TemporaryEditorDrawer::Edge::Left);
    pumpEvents();
    const QImage darkPreviewImage = dockPreview
        ? dockPreview->grab().toImage()
        : QImage();
    drawer->clearDockPreview();
    drawer->setState(TemporaryEditorDrawer::State::Floating);
    pumpEvents();
    expect("dark theme updates drawer, handle, preview, and search visual authority",
           darkDrawerSurface.isValid()
               && darkDrawerSurface != lightDrawerSurface
               && !lightHandleImage.isNull()
               && !darkHandleImage.isNull()
               && lightHandleImage != darkHandleImage
               && !lightPreviewImage.isNull()
               && !darkPreviewImage.isNull()
               && lightPreviewImage != darkPreviewImage
               && drawer->handleWidget()->palette()
                      .color(QPalette::Window)
                      == drawer->palette().color(QPalette::Window)
               && drawer->searchField()->palette()
                      .color(QPalette::Base)
                      != lightDrawerSurface);
    expect("theme switch does not rebuild editor, document, history, or split tree",
           controller.editor() == editorBeforeTheme
               && editorBeforeTheme->document() == documentBeforeTheme
               && controller.historyCount() == historyBeforeTheme
               && controller.historyIndex() == historyIndexBeforeTheme
               && tabManager.splitCount() == splitCountBefore
               && splitLayoutMatches(
                   splitBefore,
                   &editorRegion,
                   tabManager.editorSplitController()));
    expect("theme switch preserves drawer cursor, selection, and scroll",
           editorBeforeTheme->textCursor().position()
                   == cursorBeforeTheme
               && editorBeforeTheme->textCursor().anchor()
                      == anchorBeforeTheme
               && editorBeforeTheme->verticalScrollBar()->value()
                      == drawerScrollBeforeTheme);

    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    drawer->refreshTheme();
    pumpEvents(20);
    expect("Light-Dark-Light round trip restores drawer surface without state loss",
           drawer->palette().color(QPalette::Window)
                   == lightDrawerSurface
               && controller.editor() == editorBeforeTheme
               && editorBeforeTheme->document() == documentBeforeTheme
               && editorBeforeTheme->textCursor().position()
                      == cursorBeforeTheme
               && editorBeforeTheme->verticalScrollBar()->value()
                      == drawerScrollBeforeTheme);

    QTextCursor unsavedEdit(firstDocument->textDocument());
    unsavedEdit.movePosition(QTextCursor::End);
    unsavedEdit.insertText(QStringLiteral("// remains after drawer close\n"));
    const int sharedViewCountBeforeClose = firstDocument->viewCount();
    QPointer<MyCodeEditor> closingDrawerEditor(drawerEditor);
    QTextDocument* sharedTextDocumentBeforeClose =
        firstDocument->textDocument();
    bool destroyedWhileSharedDocumentAlive = false;
    QObject::connect(
        drawerEditor,
        &QObject::destroyed,
        drawer,
        [&]() {
            destroyedWhileSharedDocumentAlive =
                firstDocument->textDocument()
                == sharedTextDocumentBeforeClose;
        });
    drawer->closeButton()->click();
    expect("auxiliary close destroys the view synchronously while the shared document stays alive",
           closingDrawerEditor.isNull()
               && destroyedWhileSharedDocumentAlive
               && firstDocument->textDocument()
                      == sharedTextDocumentBeforeClose);
    pumpEvents();
    expect("Close removes only the auxiliary view and hides the drawer",
           !controller.isOpen()
               && drawer->state()
                      == TemporaryEditorDrawer::State::Hidden
               && tabManager.auxiliaryViews().isEmpty()
               && firstDocument->viewCount()
                      == sharedViewCountBeforeClose - 1);
    expect("Close does not close or copy the shared document",
           tabManager.editorCount() == tabEditorCountBefore
               && tabManager.splitCount() == splitCountBefore
               && tabManager.sharedDocumentForEditor(mainEditor)
                      == firstDocument
               && mainEditor->document()
                      == firstDocument->textDocument()
               && firstDocument->dirty()
               && mainEditor->toPlainText().contains(
                   QStringLiteral("remains after drawer close")));
    expect("complete drawer lifecycle leaves splitter count, groups, and sizes unchanged",
           splitLayoutMatches(
               splitBefore,
               &editorRegion,
               tabManager.editorSplitController()));

    tabManager.unsavedDocumentManagerForTesting()
        ->setDecisionProvider(
            [](const QList<PendingDocumentChange>&,
               QWidget*) {
                return UnsavedDocumentBatchDecision::DiscardAll;
            });
    expect("test teardown closes main views through TabManager",
           tabManager.closeAllTabs());
    QCoreApplication::sendPostedEvents(
        nullptr, QEvent::DeferredDelete);
    pumpEvents(20);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
