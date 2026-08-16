#include "actionregistry.h"
#include "editorsourcenavigation.h"
#include "mycodeeditor.h"

#include <QApplication>
#include <QPushButton>
#include <QTextCursor>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}

void placeCursor(MyCodeEditor* editor, int position)
{
    QTextCursor cursor(editor->document());
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    MyCodeEditor editor;
    EditorAnnotationDisplayOptions annotationOptions;
    annotationOptions.enabled = false;
    annotationOptions.maxAnnotationsPerLine = 0;
    annotationOptions.maxLanes = 99;
    editor.setAnnotationDisplayOptions(annotationOptions);
    const EditorAnnotationDisplayOptions normalizedOptions =
        editor.annotationDisplayOptions();
    expect("editor annotation settings are normalized and applied",
           !normalizedOptions.enabled
               && normalizedOptions.maxAnnotationsPerLine == 1
               && normalizedOptions.maxLanes == 16);

    editor.setPlainText(
        QStringLiteral("module plain; logic value; endmodule\n"));
    placeCursor(&editor, 0);
    EditorSourceNavigationUi sourceNavigation;
    EditorHoverPopup* definitionPreview =
        sourceNavigation.beginExternalPeek(
            &editor, true);
    DefinitionPreviewReport previewReport;
    previewReport.available = true;
    previewReport.targetResolved = true;
    previewReport.symbolName = QStringLiteral("value");
    previewReport.displayKind = QStringLiteral("signal");
    previewReport.targetFile =
        QStringLiteral("C:/rtl/workspace/definition.sv");
    previewReport.targetLine = 23;
    previewReport.targetColumn = 7;
    previewReport.firstLineNumber = 23;
    previewReport.highlightedLine = 23;
    previewReport.codeLines = {
        QStringLiteral("logic value;")};
    definitionPreview->showPreview(
        previewReport,
        QPoint(10, 10),
        editor.font());

    const QString temporaryEditorActionId =
        QString::fromLatin1(
            ActionIds::ViewTemporaryEditorOpen);
    const PeekContentModel previewContent =
        definitionPreview->contentModel();
    const auto action = std::find_if(
        previewContent.actions.cbegin(),
        previewContent.actions.cend(),
        [&temporaryEditorActionId](
            const PeekContentAction& candidate) {
            return candidate.id
                == temporaryEditorActionId;
        });
    expect("definition preview exposes the unified temporary-editor Action",
           action != previewContent.actions.cend()
               && !action->label.isEmpty()
               && previewContent.navigationTarget.fileName
                      == previewReport.targetFile
               && previewContent.navigationTarget.line
                      == previewReport.targetLine
               && previewContent.navigationTarget.column
                      == previewReport.targetColumn);

    QString requestedActionId;
    QVariantMap requestedParameters;
    QObject::connect(
        &editor,
        &MyCodeEditor::registeredActionRequested,
        &editor,
        [&](const QString& actionId,
            const QVariantMap& parameters,
            bool* handled) {
            requestedActionId = actionId;
            requestedParameters = parameters;
            if (handled)
                *handled = true;
        });
    QPushButton* temporaryEditorButton =
        definitionPreview->findChild<QPushButton*>(
            QStringLiteral("peekAction.%1")
                .arg(temporaryEditorActionId));
    if (temporaryEditorButton)
        temporaryEditorButton->click();
    expect("definition preview routes its target through the Registry Action",
           temporaryEditorButton
               && requestedActionId
                      == temporaryEditorActionId
               && requestedParameters
                      .value(QStringLiteral("path"))
                      .toString()
                      == previewReport.targetFile
               && requestedParameters
                      .value(QStringLiteral("line"))
                      .toInt()
                      == previewReport.targetLine
               && requestedParameters
                      .value(QStringLiteral("column"))
                      .toInt()
                      == previewReport.targetColumn);

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
