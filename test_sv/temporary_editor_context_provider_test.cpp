#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "editorcoordinator.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorcontextview.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QMainWindow>
#include <QPointer>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <iostream>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool writeFixture(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray text(
        "module context_fixture;\n"
        "    logic first_signal;\n"
        "    logic second_signal;\n"
        "endmodule\n");
    return file.write(text) == text.size();
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    check(temporaryDirectory.isValid(),
          "temporary directory is available");
    const QString filePath = temporaryDirectory.filePath(
        QStringLiteral("context_fixture.sv"));
    check(writeFixture(filePath),
          "shared-document fixture is written");

    QMainWindow window;
    auto* editorRegion = new QWidget(&window);
    auto* layout = new QVBoxLayout(editorRegion);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(editorRegion);
    layout->addWidget(tabs);
    window.setCentralWidget(editorRegion);
    window.resize(1000, 700);

    TabManager tabManager(tabs);
    tabManager.enableSplitLayout(editorRegion);
    EditorCoordinator editorCoordinator(&tabManager);
    editorCoordinator.connectSignals();
    check(tabManager.openFileInTab(filePath),
          "fixture opens in the primary editor");
    window.show();
    QApplication::processEvents();

    MyCodeEditor* primaryEditor = tabManager.getCurrentEditor();
    SharedDocument* shared =
        tabManager.sharedDocumentForEditor(primaryEditor);
    check(primaryEditor && shared,
          "primary editor has a shared document");
    const ContextResource currentEditorResource =
        TemporaryEditorContextProvider::resourceForCurrentEditor(
            &tabManager, temporaryDirectory.path());
    const EditorLocation currentEditorLocation =
        TemporaryEditorContextProvider::locationFromResource(
            currentEditorResource);
    check(currentEditorResource.isValid()
              && currentEditorLocation.documentId
                     == shared->documentId()
              && EditorFileIdentity::same(
                  currentEditorLocation.filePath, filePath),
          "provider derives a portable resource from the active editor");

    ContextWorkspaceController controller(
        &window, editorRegion, &window);
    controller.setWorkspaceRoot(temporaryDirectory.path());
    auto provider =
        std::make_unique<TemporaryEditorContextProvider>(&tabManager);
    check(controller.registerProvider(std::move(provider)),
          "temporary-editor provider registers");

    EditorLocation first;
    first.documentId = shared ? shared->documentId() : QString();
    first.filePath = filePath;
    first.line = 2;
    first.column = 11;
    const ContextResource firstResource =
        TemporaryEditorContextProvider::resourceForLocation(
            first, temporaryDirectory.path());
    check(controller.openResource(
              firstResource, ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global}),
          "temporary editor opens in Peek");

    auto* contextView = qobject_cast<TemporaryEditorContextView*>(
        controller.peekHost()->view());
    MyCodeEditor* contextEditor =
        contextView ? contextView->editor() : nullptr;
    check(contextView && contextEditor
              && contextEditor->document()
                     == primaryEditor->document()
              && tabManager.isAuxiliaryView(contextEditor),
          "Peek uses the authoritative shared QTextDocument");
    QToolButton* unavailableFullView =
        controller.peekHost()->findChild<QToolButton*>(
            QStringLiteral("contextPeekFullView"));
    check(unavailableFullView && !unavailableFullView->isVisible(),
          "temporary editor does not expose an unsupported full-view action");
    check(contextView->currentLocation().equivalentTo(first)
              && contextEditor->textCursor().blockNumber() == 1,
          "resource location positions the shared auxiliary view");

    const QString inserted = QStringLiteral("// shared edit\n");
    QTextCursor primaryCursor(primaryEditor->document());
    primaryCursor.movePosition(QTextCursor::Start);
    primaryCursor.insertText(inserted);
    check(contextEditor->toPlainText().startsWith(inserted),
          "main-editor changes are visible in Peek without copying text");

    EditorLocation second = first;
    second.line = 4;
    second.column = 5;
    const ContextResource secondResource =
        TemporaryEditorContextProvider::resourceForLocation(
            second, temporaryDirectory.path());
    QPointer<TemporaryEditorContextView> originalView = contextView;
    check(controller.openResource(
              secondResource, ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global})
              && controller.peekHost()->view() == originalView
              && contextView->historyCount() == 2
              && contextView->canGoBack(),
          "same stable resource reuses its view and appends history");
    int backNotifications = 0;
    QObject::connect(contextView, &TemporaryEditorContextView::currentLocationChanged,
                     &window, [&backNotifications](const EditorLocation&) { ++backNotifications; });
    contextView->backButton()->click();
    check(contextView->currentLocation().refersToSameDocument(first)
              && contextView->currentLocation().line == first.line
              && controller.peekHost()->resource().title
                     == TemporaryEditorContextProvider::resourceForCurrentEditor(
                         &tabManager, temporaryDirectory.path()).title,
          "Back navigation updates both the view and common Peek title");
    check(backNotifications == 1
              && contextView->currentLocation().equivalentTo(first)
              && controller.peekHost()->resource().stableKey() == firstResource.stableKey()
              && TemporaryEditorContextProvider::locationFromResource(
                     controller.peekHost()->resource()).equivalentTo(first),
          "Back notification preserves identity and updates the common resource location");

    check(controller.pinPeek()
              && controller.dockHost()->resourceCount() == 1
              && controller.dockHost()->viewForResource(
                     firstResource.stableKey()) == originalView
              && controller.peekHost()->view() == nullptr,
          "pinning moves the exact shared editor view into the dock");
    const ContextWorkspaceState persistedState = controller.captureState();
    const ContextResource persistedResource =
        ContextResource::fromVariantMap(
            persistedState.pinnedResources.value(0));
    const QVariantMap persistedLocation =
        persistedResource.state.value(
            QStringLiteral("location")).toMap();
    check(persistedState.pinnedResources.size() == 1
              && persistedLocation.value(
                     QStringLiteral("workspaceRelativePath")).toString()
                     == QStringLiteral("context_fixture.sv")
              && !persistedLocation.contains(QStringLiteral("path"))
              && !persistedLocation.contains(
                  QStringLiteral("documentId")),
          "pinned temporary editor persists a workspace-relative identity only");
    QToolButton* unpinButton =
        controller.dockHost()->findChild<QToolButton*>(
            QStringLiteral("contextDockUnpin"));
    if (unpinButton)
        unpinButton->click();
    check(unpinButton
              && controller.peekHost()->view() == originalView
              && controller.dockHost()->resourceCount() == 0,
          "Dock unpin action returns the exact view to Peek");

    controller.closePeek();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    check(originalView.isNull()
              && tabManager.auxiliaryViews().isEmpty(),
          "closing context releases the auxiliary view cleanly");

    QTemporaryDir relocatedDirectory;
    const QString relocatedFile = relocatedDirectory.filePath(
        QStringLiteral("context_fixture.sv"));
    check(relocatedDirectory.isValid()
              && writeFixture(relocatedFile),
          "relocated workspace fixture is available");
    controller.setWorkspaceRoot(relocatedDirectory.path());
    const ContextWorkspaceRestoreResult restoreResult =
        controller.restoreState(persistedState);
    auto* relocatedView = qobject_cast<TemporaryEditorContextView*>(
        controller.dockHost()->viewForResource(
            firstResource.stableKey()));
    check(restoreResult.restoredResources == 1
              && restoreResult.skippedResources == 0
              && relocatedView
              && EditorFileIdentity::same(
                  relocatedView->currentLocation().filePath,
                  relocatedFile),
          "pinned temporary editor follows a relocated workspace root");
    controller.clearResources();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    check(tabManager.auxiliaryViews().isEmpty(),
          "restored context cleans up its relocated auxiliary view");

    if (failures == 0) {
        std::cout << "temporary_editor_context_provider_test: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "temporary_editor_context_provider_test: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
