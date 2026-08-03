#include "actionregistry.h"
#include "editorhoverpopup.h"
#include "effectivevalueservice.h"
#include "foldblockshelfmodel.h"
#include "foldblockshelfpanel.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QWidget>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
}

bool allLabelsUseFont(const EditorHoverPopup& peek,
                      const QFont& expected)
{
    const QList<QLabel*> labels = peek.findChildren<QLabel*>();
    if (labels.isEmpty())
        return false;
    for (const QLabel* label : labels) {
        if (label->font().family() != expected.family()
            || label->font().pointSize() != expected.pointSize()) {
            return false;
        }
    }
    return true;
}
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);

    QWidget host;
    host.resize(900, 520);
    host.setFocusPolicy(Qt::StrongFocus);
    host.show();
    QApplication::processEvents();

    EditorHoverPopup peek(&host);
    QFont editorFont(QStringLiteral("Consolas"), 12);

    DefinitionPreviewReport definition;
    definition.available = true;
    definition.symbolName = QStringLiteral("counter_q");
    definition.displayKind = QStringLiteral("logic");
    definition.targetFile = QStringLiteral("rtl/top.sv");
    definition.targetLine = 42;
    definition.targetColumn = 9;
    definition.firstLineNumber = 41;
    definition.highlightedLine = 42;
    definition.codeLines = {
        QStringLiteral("always_ff @(posedge clk) begin"),
        QStringLiteral("  counter_q <= counter_d;"),
        QStringLiteral("end")};
    const QRect definitionAnchor(
        host.mapToGlobal(QPoint(320, 190)),
        QSize(14, 18));
    peek.showPreview(definition,
                     definitionAnchor.bottomRight(),
                     editorFont);
    QApplication::processEvents();

    expect("definition uses embedded container",
           peek.isVisible()
               && !peek.isWindow()
               && peek.parentWidget() == &host
               && peek.window() == host.window());
    expect("definition is represented by the shared model",
           peek.contentModel().kind
                   == PeekContentKind::DefinitionPreview
               && peek.contentModel().navigationTarget.fileName
                      == definition.targetFile
               && peek.hasNavigableTarget());
    expect("peek remains inside its host",
           host.rect().contains(peek.geometry()));
    expect("peek content uses the editor font",
           allLabelsUseFont(peek, editorFont));
    QToolButton* initialClose = peek.findChild<QToolButton*>(
        QStringLiteral("peekCloseButton"));
    expect("peek never requests keyboard focus",
           peek.focusPolicy() == Qt::NoFocus
               && initialClose
               && initialClose->focusPolicy() == Qt::NoFocus);

    EditorHoverPopup* originalContainer = &peek;
    peek.showNumericLiteral(
        QStringLiteral("1010_1010\n252\nfa"),
        host.mapToGlobal(QPoint(400, 230)),
        editorFont);
    QApplication::processEvents();
    expect("numeric radix reuses the definition container",
           &peek == originalContainer
               && peek.contentModel().kind
                      == PeekContentKind::NumericRadix
               && !peek.hasNavigableTarget());

    CodePreviewReport graphPreview;
    graphPreview.available = true;
    graphPreview.title = QStringLiteral("Kernel: counter_q");
    graphPreview.fileName = QStringLiteral("rtl/top.sv");
    graphPreview.targetLine = 42;
    graphPreview.targetColumn = 9;
    graphPreview.firstLineNumber = 42;
    graphPreview.highlightedLine = 42;
    graphPreview.codeLines = {
        QStringLiteral("counter_q <= counter_d;")};
    peek.showCodePreview(
        graphPreview,
        host.mapToGlobal(QPoint(410, 240)),
        editorFont);
    QApplication::processEvents();
    expect("graph code preview uses the embedded model",
           peek.contentModel().kind
                   == PeekContentKind::CodePreview
               && !peek.isWindow()
               && peek.hasNavigableTarget());

    SymbolHoverReport effective;
    effective.available = true;
    effective.symbolName = QStringLiteral("WIDTH");
    effective.displayKind = QStringLiteral("parameter");
    effective.parameterLike = true;
    effective.effectiveValueStatus = EffectiveValueStatus::Current;
    effective.valueText = QStringLiteral("16");
    peek.showHover(effective,
                   host.mapToGlobal(QPoint(420, 250)),
                   editorFont);
    QApplication::processEvents();
    expect("effective value uses the shared content model",
           peek.contentModel().kind
               == PeekContentKind::EffectiveValue);

    peek.showDiagnosticDetail(
        QStringLiteral("Width mismatch"),
        QStringLiteral("Expected 16 bits, got 8."),
        {QStringLiteral("rtl/top.sv:51")},
        QRect(host.mapToGlobal(QPoint(440, 270)), QSize(12, 16)),
        editorFont);
    expect("diagnostic adapter has a dedicated model kind",
           peek.contentModel().kind
                   == PeekContentKind::DiagnosticDetail
               && !peek.contentModel().rows.isEmpty());

    PeekNavigationTarget declarationTarget{
        QStringLiteral("rtl/pkg.sv"), 12, 3};
    peek.showDeclarationPreview(
        QStringLiteral("Declaration"),
        QStringLiteral("typedef logic [15:0] word_t;"),
        declarationTarget,
        QRect(host.mapToGlobal(QPoint(460, 290)), QSize(12, 16)),
        editorFont);
    expect("declaration adapter shares navigation semantics",
           peek.contentModel().kind
                   == PeekContentKind::DeclarationPreview
               && peek.contentModel().navigationTarget.isValid()
               && peek.hasNavigableTarget());

    PeekContentModel editableDeclaration;
    editableDeclaration.kind =
        PeekContentKind::DeclarationPreview;
    editableDeclaration.title =
        QStringLiteral("Declare signal");
    editableDeclaration.rows.append(
        {QStringLiteral("Enter confirms; Esc cancels."),
         PeekContentRowRole::Muted,
         false});
    editableDeclaration.editor.enabled = true;
    editableDeclaration.editor.text =
        QStringLiteral("logic counter_q;");
    editableDeclaration.editor.objectName =
        QStringLiteral("signalDefinitionInlineEditor");
    editableDeclaration.editor.minimumWidth = 280;
    editableDeclaration.maximumSize = QSize(420, 160);
    host.setFocus();
    peek.showContent(
        editableDeclaration,
        QRect(host.mapToGlobal(QPoint(470, 300)),
              QSize(12, 16)),
        editorFont);
    QLineEdit* declarationEditor = peek.editableLineEdit();
    if (declarationEditor) {
        declarationEditor->setFocus(Qt::PopupFocusReason);
        declarationEditor->selectAll();
        declarationEditor->setText(
            QStringLiteral("logic signed counter_q;"));
    }
    QApplication::processEvents();
    expect("editable declaration is content of the same peek",
           declarationEditor
               && declarationEditor->parentWidget() == &peek
               && declarationEditor->text()
                      == QStringLiteral("logic signed counter_q;")
               && declarationEditor->font().family()
                      == editorFont.family()
               && peek.contentModel().editor.enabled
               && peek.contentModel().editor.text
                      == declarationEditor->text()
               && !peek.isWindow());

    QKeyEvent escape(QEvent::KeyPress,
                     Qt::Key_Escape,
                     Qt::NoModifier);
    QApplication::sendEvent(
        declarationEditor ? declarationEditor : &host,
        &escape);
    QApplication::processEvents();
    expect("Escape closes editable peek and restores host focus",
           !peek.isVisible()
               && host.hasFocus());

    peek.showNumericLiteral(
        QStringLiteral("0"),
        host.mapToGlobal(QPoint(480, 310)),
        editorFont);
    QToolButton* close = peek.findChild<QToolButton*>(
        QStringLiteral("peekCloseButton"));
    expect("shared close control exists", close != nullptr);
    if (close)
        close->click();
    QApplication::processEvents();
    expect("shared close control hides the container",
           !peek.isVisible());

    FoldBlockShelfModel shelfModel;
    FoldShelfItem shelfItem;
    shelfItem.alias = QStringLiteral("counter update");
    shelfItem.text = QStringLiteral(
        "always_ff @(posedge clk) begin\n"
        "  counter_q <= counter_d;\n"
        "end\n");
    shelfItem.sourceFile = QStringLiteral("rtl/top.sv");
    shelfItem.sourceModule = QStringLiteral("top");
    shelfItem.sourceStartLine = 41;
    shelfItem.sourceEndLine = 43;
    shelfItem.originKind = FoldShelfOriginKind::Moved;
    shelfModel.addItem(shelfItem);

    FoldBlockShelfPanel shelfPanel;
    shelfPanel.resize(900, 520);
    shelfPanel.setModel(&shelfModel);
    shelfPanel.show();
    QApplication::processEvents();
    QListWidget* shelfList = shelfPanel.findChild<QListWidget*>(
        QStringLiteral("foldShelfListWidget"));
    if (shelfList && shelfList->count() > 0) {
        shelfList->setCurrentRow(0);
        shelfList->itemDoubleClicked(shelfList->item(0));
    }
    QApplication::processEvents();

    EditorHoverPopup* shelfPeek =
        shelfPanel.findChild<EditorHoverPopup*>(
            QString(), Qt::FindDirectChildrenOnly);
    QPlainTextEdit* shelfText = shelfPeek
        ? shelfPeek->findChild<QPlainTextEdit*>(
              QStringLiteral("foldShelfPreviewText"))
        : nullptr;
    expect("Fold Shelf preview uses the embedded Peek container",
           shelfPeek
               && shelfPeek->isVisible()
               && !shelfPeek->isWindow()
               && shelfPeek->parentWidget() == &shelfPanel
               && shelfPeek->contentModel().kind
                      == PeekContentKind::FoldShelfPreview
               && shelfPeek->contentModel().title
                      == QStringLiteral("Fold Block: counter update")
               && shelfPanel.findChildren<QDialog*>().isEmpty());
    expect("Fold Shelf Peek preserves complete scrollable source text",
           shelfText
               && shelfText->isReadOnly()
               && shelfText->toPlainText() == shelfItem.text
               && shelfText->font().family()
                      == shelfPanel.font().family()
               && !shelfText->hasFocus()
               && shelfPeek->contentModel().readOnlyText.text
                      == shelfItem.text);
    expect("Fold Shelf Peek exposes source and state metadata",
           shelfPeek
               && shelfPeek->contentModel().rows.size() == 3
               && shelfPeek->contentModel().rows.at(0).text
                      == QStringLiteral("%1:41-43").arg(
                          shelfModel.items().constFirst().sourceFile)
               && shelfPeek->contentModel().rows.at(1).text
                      == QStringLiteral("module: top")
               && shelfPeek->contentModel().rows.at(2).text
                      == QStringLiteral("3 lines · moved"));

    FoldShelfItem deleteItem;
    deleteItem.alias = QStringLiteral("delete candidate");
    deleteItem.text = QStringLiteral("logic delete_candidate;\n");
    deleteItem.sourceFile = QStringLiteral("rtl/delete.sv");
    deleteItem.sourceModule = QStringLiteral("delete_fixture");
    deleteItem.sourceStartLine = 8;
    deleteItem.sourceEndLine = 8;
    deleteItem.originKind = FoldShelfOriginKind::Copied;
    shelfModel.addItem(deleteItem);
    QApplication::processEvents();
    if (shelfList && shelfList->count() > 0)
        shelfList->setCurrentRow(shelfList->count() - 1);

    QString requestedShelfAction;
    shelfPanel.setActionRequestHandler(
        [&](const QString& actionId,
            QString* failureReason) {
        requestedShelfAction = actionId;
        return shelfPanel.deleteSelectedItem(
            failureReason);
    });
    QVariantMap shortcutOverrides;
    shortcutOverrides.insert(
        QString::fromLatin1(
            ActionIds::FoldShelfDeleteSelected),
        QStringLiteral("Ctrl+Delete"));
    QStringList shortcutIssues;
    const bool shortcutOverrideAccepted =
        configureActionShortcutOverrides(
            shortcutOverrides,
            &shortcutIssues);
    const int itemsBeforeDelete =
        shelfModel.items().size();
    QKeyEvent oldDelete(
        QEvent::KeyPress,
        Qt::Key_Delete,
        Qt::NoModifier);
    QApplication::sendEvent(shelfList, &oldDelete);
    const bool oldDeleteInactive =
        requestedShelfAction.isEmpty()
        && shelfModel.items().size()
               == itemsBeforeDelete;
    QKeyEvent remappedDelete(
        QEvent::KeyPress,
        Qt::Key_Delete,
        Qt::ControlModifier);
    QApplication::sendEvent(
        shelfList, &remappedDelete);
    configureActionShortcutOverrides({});
    expect("Fold Shelf Delete consumes its Registry shortcut override",
           shortcutOverrideAccepted
               && shortcutIssues.isEmpty()
               && oldDeleteInactive
               && requestedShelfAction
                      == QString::fromLatin1(
                          ActionIds::FoldShelfDeleteSelected)
               && shelfModel.items().size()
                      == itemsBeforeDelete - 1);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
