#include "actionregistry.h"
#include "testuistyle.h"
#include "applicationthememanager.h"
#include "uitypography.h"
#include <QDir>
#include <QScrollArea>
#include "editorhoverpopup.h"
#include "effectivevalueservice.h"

#include <QApplication>
#include <QClipboard>
#include <QTextBlock>
#include <QTextLayout>
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>
#include <QtTest/QTest>

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
        if (!label->isVisible()) continue;
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
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 2;

    QWidget host;
    host.resize(900, 520);
    host.setFocusPolicy(Qt::StrongFocus);
    host.show();
    QApplication::processEvents();
    std::printf("platform=%s actualDpr=%.2f\n", qPrintable(QGuiApplication::platformName()), host.devicePixelRatioF());
    if (qEnvironmentVariableIsSet("ZEROSLACK_EXPECTED_DPR"))
        expect("requested DPR matches the real widget", qAbs(host.devicePixelRatioF() - qEnvironmentVariable("ZEROSLACK_EXPECTED_DPR").toDouble()) < 0.01);

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

    DefinitionPreviewReport longDefinition = definition;
    longDefinition.symbolName = QStringLiteral("downstream_wr_data");
    longDefinition.codeLines.clear();
    for (int i=0;i<60;++i)
        longDefinition.codeLines.append(QString(70,QLatin1Char(' ')) + QStringLiteral("logic [1023:0] downstream_wr_data;") + QString(180,QLatin1Char(' ')) + QStringLiteral("// complete tail"));
    longDefinition.highlightedLine = longDefinition.firstLineNumber + 42;
    longDefinition.targetLine = longDefinition.highlightedLine;
    longDefinition.targetColumn = 85;
    QHash<QString,QSize> previewSizes;
    for (int i=0;i<9;++i) {
        const auto& report = i%3 == 1 ? longDefinition : definition;
        peek.showPreview(report,host.mapToGlobal(QPoint(880,500)),editorFont);
        QApplication::processEvents();
        auto* code = peek.findChild<QPlainTextEdit*>(QStringLiteral("peekDefinitionCode"));
        auto* action = peek.findChild<QPushButton*>();
        if (code && action) std::printf("preview %d popup=%dx%d code=%dx%d action=%dx%d hint=%dx%d scroll=%d/%d cursor=%d,%d\n",i,
            peek.width(),peek.height(),code->width(),code->height(),action->width(),action->height(),action->sizeHint().width(),action->sizeHint().height(),
            code->horizontalScrollBar()->maximum(),code->verticalScrollBar()->maximum(),code->cursorRect().x(),code->cursorRect().y());
        expect("each definition keeps full code and a usable footer", code && action
               && code->toPlainText().endsWith(report.codeLines.last())
               && code->extraSelections().size() == 1
               && code->extraSelections().first().cursor.blockNumber() == report.highlightedLine-report.firstLineNumber
               && action->height() >= action->sizeHint().height()
               && host.rect().contains(peek.geometry())
               && peek.rect().contains(QRect(action->mapTo(&peek,QPoint()),action->size()))
               && !QRect(action->mapTo(&peek,QPoint()),action->size()).intersects(QRect(code->mapTo(&peek,QPoint()),code->size())));
        if (previewSizes.contains(report.symbolName))
            expect("repeated target changes never shrink the same content", peek.size() == previewSizes.value(report.symbolName));
        previewSizes.insert(report.symbolName,peek.size());
        if (code && i%3 == 1) {
            expect("long preview has both scroll ranges and reveals the declaration", code->horizontalScrollBar()->maximum()>0
                   && code->verticalScrollBar()->maximum()>0 && code->viewport()->rect().contains(code->cursorRect().center()));
            code->horizontalScrollBar()->setValue(code->horizontalScrollBar()->maximum());
            code->verticalScrollBar()->setValue(code->verticalScrollBar()->maximum());
            expect("the complete code tail remains reachable", code->cursorForPosition(QPoint(2,2)).blockNumber()>0
                   && code->toPlainText().contains(QStringLiteral("// complete tail")));
        }
    }
    DefinitionPreviewReport unavailable;
    unavailable.symbolName = QStringLiteral("unresolved");
    unavailable.unavailableReason = QStringLiteral("Definition unavailable.");
    peek.showPreview(unavailable,host.mapToGlobal(QPoint(20,20)),editorFont);
    QApplication::processEvents();
    expect("unavailable target discards previous code and actions", !peek.findChild<QPlainTextEdit*>(QStringLiteral("peekDefinitionCode"))
           && !peek.findChild<QPushButton*>() && !peek.contentModel().navigationTarget.isValid());

    EditorHoverPopup* originalContainer = &peek;
    peek.showNumericLiteral(
        QStringLiteral("character: ~\n"
                       "binary: 0111_1110\n"
                       "octal: 0176\n"
                       "decimal: 126\n"
                       "hex: 7e"),
        host.mapToGlobal(QPoint(400, 230)),
        editorFont);
    QApplication::processEvents();
    expect("numeric radix reuses the definition container",
           &peek == originalContainer
               && peek.contentModel().kind
                      == PeekContentKind::NumericRadix
               && !peek.hasNavigableTarget());
    expect("ASCII numeric radix reserves height for the hexadecimal row",
           peek.contentModel().maximumSize.height() > 160
               && peek.contentModel().rows.size() == 1
               && peek.contentModel().rows.first().text
                      .endsWith(QStringLiteral("hex: 7e")));

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

    effective.symbolName = QStringLiteral("P_CLK_PERIOD_UNIT_NS");
    effective.valueText = QStringLiteral("8");
    effective.ownerName = QStringLiteral("byte2uart");
    effective.declarationText = QStringLiteral("parameter P_CLK_PERIOD_UNIT_NS = 8");
    effective.resolvedTypeText = QStringLiteral("logic signed[31:0]");
    effective.bitWidthText = QStringLiteral("32");
    effective.expressionText = QStringLiteral("8");
    effective.instancePath = QStringLiteral("byte2uart");
    effective.valueSource = QStringLiteral("slang elaborated parameter default");
    effective.defaultEvaluation = true;
    effective.definitionFile = QStringLiteral("rtl/byte2uart.sv");
    effective.definitionLine = 26;
    QString navigatedFile;
    peek.setNavigationHandler([&](const QString& file, int line, int) {
        navigatedFile = file;
        Q_UNUSED(line)
    });
    auto showCard = [&]() {
        peek.showHover(effective, host.mapToGlobal(QPoint(20, 20)), editorFont);
        QApplication::processEvents();
    };
    auto snapshot = [&](const QString& name) {
        const QString directory = qEnvironmentVariable("ZEROSLACK_PEEK_PREVIEW_DIR");
        if (!directory.isEmpty()) {
            QDir().mkpath(directory);
            QApplication::processEvents();
            expect("popover preview saved", host.grab(peek.geometry().adjusted(-10, -10, 10, 10).intersected(host.rect())).save(directory + QLatin1Char('/') + name + QStringLiteral(".png")));
        }
    };
    ApplicationThemeManager::instance().applyToApplication();
    showCard();
    auto* toggle = peek.findChild<QToolButton*>(QStringLiteral("peekDetailsToggle"));
    expect("default parameter is direct information without declaration or navigation buttons",
           !toggle && !peek.findChild<QToolButton*>(QStringLiteral("peekSourceLink"))
           && !peek.hasNavigableTarget() && peek.contentModel().title == QStringLiteral("Default 8"));
    auto* cardTitle = peek.findChild<QPlainTextEdit*>(QStringLiteral("peekTitleText"));
    bool unifiedTypography = true;
    for (auto* text : peek.findChildren<QPlainTextEdit*>())
        unifiedTypography &= text->font().family()==editorFont.family()
            && text->font().pointSizeF()==editorFont.pointSizeF() && text->font().pixelSize()==editorFont.pixelSize();
    expect("all card text uses the editor family and size", unifiedTypography
           && cardTitle && !cardTitle->toPlainText().contains(effective.symbolName));
    snapshot(QStringLiteral("parameter-default-light"));
    QTest::mouseDClick(cardTitle->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(12, cardTitle->height()/2));
    expect("body double click selects text without navigating", navigatedFile.isEmpty()
           && peek.isVisible() && !cardTitle->textCursor().selectedText().isEmpty());
    const auto selectedCardText = cardTitle->textCursor().selectedText();
    QTest::keyClick(cardTitle, Qt::Key_C, Qt::ControlModifier);
    expect("copy keeps the card open", peek.isVisible()
           && QApplication::clipboard()->text() == selectedCardText);
    QTest::keyClick(cardTitle, Qt::Key_X);
    expect("typing closes a focused card and returns editor focus", !peek.isVisible() && host.hasFocus());

    effective.defaultEvaluation = false;
    effective.instanceBound = true;
    effective.valueText = QStringLiteral("12");
    effective.instancePath = QStringLiteral("top.u1");
    effective.valueSource = QStringLiteral("slang elaborated parameter override");
    showCard();
    expect("bound parameter identifies the effective value", peek.contentModel().title == QStringLiteral("Effective 12"));
    snapshot(QStringLiteral("parameter-effective"));
    effective.effectiveValueStatus = EffectiveValueStatus::Stale;
    showCard();
    expect("stale parameter never displays the previous value", peek.contentModel().title == QStringLiteral("Analysis stale")
           && peek.findChild<QPlainTextEdit*>(QStringLiteral("peekNotice")));
    snapshot(QStringLiteral("parameter-stale"));
    effective.effectiveValueStatus = EffectiveValueStatus::Error;
    effective.evaluationFailureReason = QStringLiteral("Constant evaluation failed.");
    showCard();
    expect("evaluation errors remain explicit", peek.contentModel().title == QStringLiteral("Information unavailable")
           && peek.findChild<QPlainTextEdit*>(QStringLiteral("peekNotice"))->toPlainText() == effective.evaluationFailureReason);

    effective = SymbolHoverReport();
    effective.available = true;
    effective.symbolName = QStringLiteral("E_AXI_LITE_STATUS_NOT_RESP");
    effective.enumMember = true;
    effective.effectiveValueStatus = EffectiveValueStatus::Current;
    effective.valueText = QStringLiteral("4'b1111");
    effective.enumTypeName = QStringLiteral("axi_lite_status_e");
    effective.enumMembers = {QStringLiteral("E_AXI_LITE_STATUS_OK = 4'b0"), QStringLiteral("E_AXI_LITE_STATUS_NOT_RESP = 4'b1111")};
    effective.definitionFile = QStringLiteral("rtl/axi_lite.sv");
    effective.definitionLine = 3;
    effective.commentText = QStringLiteral("No response from the target.\nFurther explanation.");
    showCard();
    toggle = peek.findChild<QToolButton*>(QStringLiteral("peekDetailsToggle"));
    expect("enum shows its value and type with members on demand", peek.contentModel().title == effective.valueText
           && peek.findChild<QPlainTextEdit*>(QStringLiteral("peekSummaryRow"))->toPlainText() == effective.enumTypeName
           && toggle && toggle->text() == QStringLiteral("Enum members")
           && !peek.findChild<QWidget*>(QStringLiteral("peekDetails"))->isVisible());
    expect("available comment is a single summary", peek.findChild<QPlainTextEdit*>(QStringLiteral("peekComment"))->toPlainText()
           == QStringLiteral("No response from the target."));
    snapshot(QStringLiteral("enum-collapsed"));
    toggle->click();
    QApplication::processEvents();
    expect("enum list uses supplied semantic member values", peek.findChild<QPlainTextEdit*>(QStringLiteral("peekDetailRow"))->toPlainText()
           == effective.enumMembers.join(QLatin1Char('\n')));
    snapshot(QStringLiteral("enum-expanded"));

    effective = SymbolHoverReport();
    effective.available = true;
    effective.effectiveValueStatus = EffectiveValueStatus::Current;
    effective.symbolName = QStringLiteral("byte_data");
    effective.port = true;
    effective.direction = QStringLiteral("input");
    effective.resolvedTypeText = QStringLiteral("logic signed[7:0]$[3:0]");
    effective.unpackedDimensionsText = QStringLiteral("[3:0]");
    effective.valueText = QStringLiteral("SHOULD_NOT_BE_A_RUNTIME_VALUE");
    effective.declarationText = QStringLiteral("input logic signed [7:0] byte_data [3:0]");
    effective.definitionFile = QStringLiteral("rtl/byte2uart.sv");
    effective.definitionLine = 33;
    showCard();
    expect("signal summary contains full type and never a runtime value",
           peek.contentModel().title == QStringLiteral("input logic signed[7:0] [3:0]")
           && !peek.contentModel().title.contains(effective.valueText)
           && !peek.findChild<QWidget*>(QStringLiteral("peekDetails"))
           && !peek.findChild<QPlainTextEdit*>(QStringLiteral("peekComment")));
    snapshot(QStringLiteral("input-light"));
    ApplicationThemeManager::instance().setMode(ThemeMode::CatppuccinMocha);
    QApplication::processEvents();
    snapshot(QStringLiteral("input-dark"));
    expect("visible popup follows dark theme", peek.styleSheet().contains(QStringLiteral("#cdd6f4")));
    effective.effectiveValueStatus = EffectiveValueStatus::Stale;
    showCard();
    expect("stale type is suppressed with a visible status", peek.contentModel().title == QStringLiteral("Analysis stale")
           && peek.findChild<QPlainTextEdit*>(QStringLiteral("peekNotice")));
    effective.effectiveValueStatus = EffectiveValueStatus::Current;
    effective.resolvedTypeText = QStringLiteral("very_long_package_name::very_long_packet_payload_type [31:0][15:0]");
    host.resize(320, 350);
    showCard();
    toggle = peek.findChild<QToolButton*>(QStringLiteral("peekDetailsToggle"));
    if (toggle) toggle->click();
    QApplication::processEvents();
    bool completeLabels = true;
    for (auto* text : peek.findChildren<QPlainTextEdit*>()) {
        if (!text->isVisible()) continue;
        for (auto block = text->document()->begin(); block.isValid(); block = block.next()) {
            text->document()->documentLayout()->blockBoundingRect(block);
            for (int i=0;i<block.layout()->lineCount();++i)
                completeLabels &= block.layout()->lineAt(i).naturalTextWidth() <= text->viewport()->width();
        }
        completeLabels &= text->verticalScrollBar()->maximum() == 0;
        std::printf("wrapped %s viewport=%dx%d vertical=%d\n",qPrintable(text->objectName()),text->viewport()->width(),text->viewport()->height(),text->verticalScrollBar()->maximum());
    }
    expect("long types wrap without clipping in a narrow editor", completeLabels
           && host.rect().contains(peek.geometry()) && peek.findChild<QScrollArea*>(QStringLiteral("peekSymbolScroll")));
    snapshot(QStringLiteral("narrow-type"));
    effective = SymbolHoverReport();
    effective.available = true;
    effective.moduleName = QStringLiteral("uart_controller");
    showCard();
    expect("unresolved module entry has no empty actions", peek.contentModel().title == effective.moduleName
           && !peek.findChild<QToolButton*>(QStringLiteral("peekDetailsToggle"))
           && !peek.findChild<QToolButton*>(QStringLiteral("peekSourceLink")));
    QTest::mouseClick(&host, Qt::LeftButton, Qt::NoModifier, QPoint(host.width()-2,host.height()-2));
    expect("external click closes the card", !peek.isVisible());
    peek.showPreview(longDefinition,host.mapToGlobal(QPoint(315,340)),editorFont);
    QApplication::processEvents();
    const auto* narrowCode = peek.findChild<QPlainTextEdit*>(QStringLiteral("peekDefinitionCode"));
    const auto* narrowAction = peek.findChild<QPushButton*>();
    expect("definition after a compact card fits narrow bounds with complete code and actions", narrowCode && narrowAction
           && narrowCode->toPlainText().endsWith(longDefinition.codeLines.last())
           && narrowCode->horizontalScrollBar()->maximum()>0 && narrowCode->verticalScrollBar()->maximum()>0
           && narrowAction->height()>=narrowAction->sizeHint().height() && host.rect().contains(peek.geometry()));
    snapshot(QStringLiteral("narrow-definition-after-card"));
    peek.closePopup();
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    host.resize(900, 520);

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

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
