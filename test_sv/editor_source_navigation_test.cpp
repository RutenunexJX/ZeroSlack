#include "actionregistry.h"
#include "editorsourcenavigation.h"
#include "mycodeeditor.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QTextCursor>
#include <QTextBlock>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QDir>
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

    sourceNavigation.shutdown();
    MyCodeEditor live;
    live.resize(850,650);
    const QString file = QDir::current().absoluteFilePath(QStringLiteral("peek_navigation_fixture.sv"));
    const QString source = QStringLiteral(
        "module hover_chain;\n"
        "logic [255:0] alpha;\n"
        "logic signed [511:0] beta;\n"
        "logic [1023:0] gamma;\n"
        "assign alpha = beta;\n"
        "assign gamma = alpha;\n"
        "endmodule\n");
    live.setDocumentFileName(file);
    live.setPlainText(source);
    SlangManager slang;
    auto records = slang.extractSymbolRecords(file,source);
    for (auto& record : records) record.presentation.documentRevision = live.document()->revision();
    auto* index = SemanticIndex::getInstance();
    const auto savedSnapshot = index->snapshot();
    index->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(records,{},{},{{file,source}})));
    live.show();live.activateWindow();live.setFocus();
    QApplication::processEvents();
    std::printf("source navigation actualDpr=%.2f\n",live.devicePixelRatioF());
    const auto pointFor = [&](const QString& symbol) {
        QTextCursor cursor(live.document());
        cursor.setPosition(source.lastIndexOf(symbol)+1);
        return live.cursorRect(cursor).center();
    };
    const auto visiblePeek = [&]() -> EditorHoverPopup* {
        for(auto* popup:live.findChildren<EditorHoverPopup*>()) if(popup->isVisible())return popup;
        return nullptr;
    };
    const auto originalCursor = live.textCursor();
    const auto originalScroll = live.verticalScrollBar()->value();
    QTest::keyPress(&live,Qt::Key_Control);
    QHash<QString,QSize> sizes;
    for (const auto& symbol : {QStringLiteral("alpha"),QStringLiteral("beta"),QStringLiteral("gamma"),QStringLiteral("beta"),QStringLiteral("alpha")}) {
        const auto point=pointFor(symbol);
        QMouseEvent move(QEvent::MouseMove,point,live.viewport()->mapToGlobal(point),Qt::NoButton,Qt::NoButton,Qt::ControlModifier);
        QApplication::sendEvent(live.viewport(),&move);
        QApplication::processEvents();
        auto* popup=visiblePeek();
        auto* code=popup ? popup->findChild<QPlainTextEdit*>(QStringLiteral("peekDefinitionCode")) : nullptr;
        auto* action=popup ? popup->findChild<QPushButton*>() : nullptr;
        expect("held Ctrl target switch reaches the current definition with complete actions", popup && code && action
               && popup->contentModel().title.startsWith(symbol)
               && code->toPlainText().contains(QStringLiteral("logic [1023:0] gamma;"))
               && action->height() >= action->sizeHint().height());
        if (popup) {
            if (sizes.contains(symbol))expect("returning to a target restores its original size",popup->size()==sizes.value(symbol));
            sizes.insert(symbol,popup->size());
        }
    }
    expect("Ctrl hovering preserves editor focus cursor scroll and source", live.hasFocus()
           && live.textCursor().position()==originalCursor.position()
           && live.textCursor().anchor()==originalCursor.anchor()
           && live.verticalScrollBar()->value()==originalScroll && live.toPlainText()==source);
    QTest::keyRelease(&live,Qt::Key_Control);
    expect("releasing Ctrl closes the transient preview", !visiblePeek());
    QTest::mouseDClick(live.viewport(),Qt::LeftButton,Qt::NoModifier,pointFor(QStringLiteral("beta")));
    QApplication::processEvents();
    expect("double click still selects the identifier with a semantic card", visiblePeek() && visiblePeek()->contentModel().symbolCard
           && live.textCursor().selectedText()==QStringLiteral("beta") && live.hasFocus());
    if (auto* popup=visiblePeek()) {
        auto* text=popup->findChild<QPlainTextEdit*>(QStringLiteral("peekTitleText"));
        QTest::mouseDClick(text->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(12,8));
        const auto copied=text->textCursor().selectedText();
        QTest::keyClick(text,Qt::Key_C,Qt::ControlModifier);
        expect("copy from a semantic card never propagates Ctrl into definition preview", !copied.isEmpty()
               && QApplication::clipboard()->text()==copied && visiblePeek() && visiblePeek()->contentModel().symbolCard);
    }
    live.setFocus();
    QTest::keyPress(&live,Qt::Key_Control);
    const auto targetPoint=pointFor(QStringLiteral("alpha"));
    QMouseEvent move(QEvent::MouseMove,targetPoint,live.viewport()->mapToGlobal(targetPoint),Qt::NoButton,Qt::NoButton,Qt::ControlModifier);
    QApplication::sendEvent(live.viewport(),&move);
    QApplication::processEvents();
    expect("Ctrl preview after a semantic card restores definition content and size", visiblePeek()
           && !visiblePeek()->contentModel().symbolCard && visiblePeek()->size()==sizes.value(QStringLiteral("alpha")));
    QTest::keyRelease(&live,Qt::Key_Control);
    QString navigationSymbol;
    QObject::connect(&live,&MyCodeEditor::sourceNavigationRequested,&live,[&](const EditorSourceNavigationTarget& target,const EditorSemanticContext&){navigationSymbol=target.text;});
    QTest::mouseClick(live.viewport(),Qt::LeftButton,Qt::ControlModifier,pointFor(QStringLiteral("gamma")));
    expect("Ctrl left click retains definition navigation semantics", navigationSymbol==QStringLiteral("gamma") && !visiblePeek());
    index->setSnapshot(savedSnapshot);

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
