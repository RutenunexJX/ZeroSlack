#include "editorcoordinator.h"
#include "editorselection.h"
#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"
#include "tabmanager.h"

#include <QApplication>
#include <QStringList>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <algorithm>

namespace {
int checks = 0;
int failures = 0;
QStringList capturedWarnings;

void captureWarnings(QtMsgType type,
                     const QMessageLogContext&,
                     const QString& message)
{
    if (type == QtWarningMsg)
        capturedWarnings.append(message);
}

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}

bool everyEditorHas(
    const TabManager& manager,
    const EditorAnnotationDisplayOptions& expected)
{
    const QList<MyCodeEditor*> editors =
        manager.openEditors();
    if (editors.isEmpty())
        return false;
    for (const MyCodeEditor* editor : editors) {
        if (!editor
            || editor->annotationDisplayOptions()
                != expected.normalized()) {
            return false;
        }
    }
    return true;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(&host);
    layout->addWidget(tabs);

    TabManager manager(tabs);
    manager.enableSplitLayout(&host);
    EditorCoordinator coordinator(&manager);

    EditorAnnotationDisplayOptions initial;
    initial.enabled = false;
    initial.maxAnnotationsPerLine = 2;
    initial.maxLanes = 1;
    coordinator.setAnnotationDisplayOptions(initial);
    coordinator.connectSignals();

    manager.createNewTab();
    expect("new editor receives effective annotation settings",
           manager.editorCount() == 1
               && everyEditorHas(manager, initial));

    expect("split creates a second editor view",
           manager.splitCurrentView(
               EditorSplitDirection::Right)
               && manager.editorCount() == 2);
    expect("every split view receives effective annotation settings",
           everyEditorHas(manager, initial));

    EditorAnnotationDisplayOptions updated;
    updated.enabled = true;
    updated.maxAnnotationsPerLine = 9;
    updated.maxLanes = 4;
    coordinator.setAnnotationDisplayOptions(updated);
    expect("runtime annotation update reaches every split view",
           everyEditorHas(manager, updated));

    MyCodeEditor boundedEditor;
    boundedEditor.setPlainText(QStringLiteral("logic a;\n"));
    EditorSelection selection;
    SemanticDecoration staleDecoration;
    staleDecoration.role = SemanticDecorationRole::ActualSignal;
    staleDecoration.startPosition = 128;
    staleDecoration.length = 8;
    SemanticDecoration partialDecoration;
    partialDecoration.role = SemanticDecorationRole::ActualSignal;
    partialDecoration.startPosition = 6;
    partialDecoration.length = 128;
    EditorSourceNavigationTarget staleHover;
    staleHover.text = QStringLiteral("stale");
    staleHover.startPos = 128;
    staleHover.endPos = 136;

    capturedWarnings.clear();
    const QtMessageHandler previousHandler =
        qInstallMessageHandler(captureWarnings);
    selection.highlightSemanticDecorations(
        &boundedEditor,
        {staleDecoration, partialDecoration});
    selection.highlightSignalSelections(
        &boundedEditor,
        {{128, 8}, {6, 128}});
    selection.highlightHoveredSymbol(&boundedEditor, staleHover);
    qInstallMessageHandler(previousHandler);

    bool cursorsBounded = true;
    bool clampedSelectionPresent = false;
    const int documentEnd =
        boundedEditor.document()->characterCount() - 1;
    for (const QTextEdit::ExtraSelection& extra :
         boundedEditor.extraSelections()) {
        const int start = extra.cursor.selectionStart();
        const int end = extra.cursor.selectionEnd();
        cursorsBounded = cursorsBounded
            && start >= 0
            && end >= start
            && end <= documentEnd;
        clampedSelectionPresent = clampedSelectionPresent
            || (start == 6 && end == documentEnd);
    }
    expect("stale annotation ranges never create out-of-bounds cursors",
           cursorsBounded && clampedSelectionPresent);
    expect("stale annotation ranges emit no QTextCursor warnings",
           std::none_of(
               capturedWarnings.cbegin(),
               capturedWarnings.cend(),
               [](const QString& warning) {
                   return warning.contains(
                       QStringLiteral("QTextCursor::setPosition"));
               }));

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
