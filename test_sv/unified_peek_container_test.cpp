#include "actionregistry.h"
#include "applicationthememanager.h"
#include "uitypography.h"
#include <QDir>
#include <QScrollArea>
#include "editorhoverpopup.h"
#include "effectivevalueservice.h"

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
    effective.definitionFile = QStringLiteral("rtl/byte2uart.sv");
    effective.definitionLine = 26;
    QString navigatedFile;
    int navigatedLine = -1;
    peek.setNavigationHandler([&](const QString& file, int line, int) {
        navigatedFile = file;
        navigatedLine = line;
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
    auto* details = peek.findChild<QWidget*>(QStringLiteral("peekDetails"));
    auto* toggle = peek.findChild<QToolButton*>(QStringLiteral("peekDetailsToggle"));
    auto* source = peek.findChild<QToolButton*>(QStringLiteral("peekSourceLink"));
    const int collapsedHeight = peek.height();
    expect("symbol summary keeps secondary fields collapsed",
           details && !details->isVisible() && toggle && !toggle->isChecked()
           && peek.findChildren<QLabel*>(QStringLiteral("peekFieldValue")).size() == 2
           && collapsedHeight < 280);
    expect("symbol title keeps code family with a separate UI label font",
           peek.findChild<QLabel*>(QStringLiteral("peekTitle"))->font().family() == editorFont.family()
           && peek.findChild<QLabel*>(QStringLiteral("peekFieldLabel"))->font().families() == UiTypography::font().families());
    const auto* cardTitle = peek.findChild<QLabel*>(QStringLiteral("peekTitle"));
    const auto* cardFooter = peek.findChild<QWidget*>(QStringLiteral("peekFooter"));
    expect("initial layout shows complete title and footer",
           cardTitle && cardFooter && source && cardTitle->height() >= cardTitle->fontMetrics().height()
           && peek.rect().contains(cardFooter->geometry())
           && peek.rect().contains(QRect(source->mapTo(&peek, QPoint()), source->size())));
    snapshot(QStringLiteral("parameter-light"));
    if (toggle) toggle->click();
    QApplication::processEvents();
    expect("details expand without leaving the editor", details && details->isVisible()
           && peek.height() > collapsedHeight && host.rect().contains(peek.geometry()));
    snapshot(QStringLiteral("parameter-expanded"));
    if (toggle) toggle->click();
    QApplication::processEvents();
    expect("collapsing restores compact height", peek.height() == collapsedHeight);
    if (source) source->click();
    expect("source link routes exact definition", source && source->isEnabled()
           && navigatedFile == effective.definitionFile && navigatedLine == 26);

    effective.symbolName = QStringLiteral("byte_data");
    effective.displayKind = QStringLiteral("input");
    effective.parameterLike = false;
    effective.port = true;
    effective.resolvedTypeText = QStringLiteral("logic[7:0]");
    effective.signednessText = QStringLiteral("unsigned");
    effective.declarationText = QStringLiteral("input logic [7:0] byte_data");
    effective.definitionLine = 33;
    showCard();
    expect("signal summary never invents a live value", peek.contentModel().rows.size() > 0
           && peek.findChildren<QLabel*>(QStringLiteral("peekFieldValue")).size() == 2
           && !peek.findChild<QWidget*>(QStringLiteral("peekDetails"))->isVisible());
    snapshot(QStringLiteral("input-light"));
    ApplicationThemeManager::instance().setMode(ThemeMode::CatppuccinMocha);
    QApplication::processEvents();
    snapshot(QStringLiteral("input-dark"));
    expect("visible popup follows dark theme", peek.styleSheet().contains(QStringLiteral("#cdd6f4")));
    effective.effectiveValueStatus = EffectiveValueStatus::Stale;
    showCard();
    const auto* notice = peek.findChild<QLabel*>(QStringLiteral("peekNotice"));
    expect("stale facts are suppressed with a visible status", notice && notice->isVisible()
           && notice->text().contains(QStringLiteral("stale"))
           && peek.findChildren<QLabel*>(QStringLiteral("peekFieldValue")).isEmpty());
    effective.effectiveValueStatus = EffectiveValueStatus::Current;
    effective.declarationText = QString(2000, QLatin1Char('x'));
    host.resize(320, 350);
    showCard();
    toggle = peek.findChild<QToolButton*>(QStringLiteral("peekDetailsToggle"));
    if (toggle) toggle->click();
    QApplication::processEvents();
    expect("long details scroll in a narrow editor", host.rect().contains(peek.geometry())
           && peek.findChild<QScrollArea*>(QStringLiteral("peekSymbolScroll"))
           && peek.findChild<QToolButton*>(QStringLiteral("peekSourceLink"))->isVisible());
    snapshot(QStringLiteral("narrow-expanded"));
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
