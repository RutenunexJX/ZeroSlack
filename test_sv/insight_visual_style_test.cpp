#include "uitypography.h"
#include "applicationthememanager.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"
#include "myhighlighter.h"
#include "mycodeeditor.h"
#include "navigationwidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QWidget>

#include <cmath>
#include <cstdio>

namespace {
int g_checks = 0;
int g_fails = 0;

void expectTrue(const char* label, bool value)
{
    ++g_checks;
    if (!value)
        ++g_fails;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", label);
}

double linearChannel(int channel)
{
    const double normalized = channel / 255.0;
    return normalized <= 0.04045
        ? normalized / 12.92
        : std::pow((normalized + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color)
{
    return 0.2126 * linearChannel(color.red())
        + 0.7152 * linearChannel(color.green())
        + 0.0722 * linearChannel(color.blue());
}

double contrastRatio(const QColor& foreground,
                     const QColor& background)
{
    const double lighter = qMax(relativeLuminance(foreground),
                                relativeLuminance(background));
    const double darker = qMin(relativeLuminance(foreground),
                               relativeLuminance(background));
    return (lighter + 0.05) / (darker + 0.05);
}

QColor formatColorAt(const QTextDocument& document, int position)
{
    const QTextBlock block = document.findBlock(position);
    if (!block.isValid() || !block.layout())
        return {};
    const int inBlock = position - block.position();
    for (const QTextLayout::FormatRange& range :
         block.layout()->formats()) {
        if (inBlock >= range.start
            && inBlock < range.start + range.length) {
            return range.format.foreground().color();
        }
    }
    return {};
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    ApplicationThemeManager& themeManager =
        ApplicationThemeManager::instance();
    themeManager.setMode(ThemeMode::Light);

    const QFont bodyFont = UiTypography::font();
    const QFontMetrics bodyMetrics(bodyFont);
    expectTrue("UI text uses a proportional face instead of the editor monospace",
               bodyMetrics.horizontalAdvance(QStringLiteral("WWW"))
                   > bodyMetrics.horizontalAdvance(QStringLiteral("iii")) * 1.5);
    QLabel roleLabel;
    UiTypography::apply(&roleLabel, UiTypography::Role::Metadata);
    const QFont metadataFont = roleLabel.font();
    UiTypography::apply(&roleLabel, UiTypography::Role::PanelTitle);
    expectTrue("panel titles have stronger hierarchy than supporting text",
               roleLabel.font().pixelSize() > metadataFont.pixelSize()
                   && roleLabel.font().weight() > metadataFont.weight());
    const InsightTheme& lightTheme =
        InsightVisualStyle::theme(ThemeMode::Light);
    const InsightTheme& darkTheme =
        InsightVisualStyle::theme(ThemeMode::Dark);
    expectTrue("light and dark theme surfaces differ",
               lightTheme.appBackground != darkTheme.appBackground
                   && lightTheme.panelBackground
                          != darkTheme.panelBackground
                   && lightTheme.textPrimary != darkTheme.textPrimary);
    expectTrue("light primary text has readable contrast",
               contrastRatio(lightTheme.textPrimary,
                             lightTheme.panelBackground) >= 4.5);
    expectTrue("dark primary text has readable contrast",
               contrastRatio(darkTheme.textPrimary,
                             darkTheme.panelBackground) >= 4.5);
    expectTrue("input text has readable contrast in both themes",
               contrastRatio(lightTheme.input.text,
                             lightTheme.input.background) >= 4.5
                   && contrastRatio(darkTheme.input.text,
                                    darkTheme.input.background) >= 4.5);
    expectTrue("selected text has readable contrast in both themes",
               contrastRatio(lightTheme.input.selectionText,
                             lightTheme.input.selectionBackground) >= 4.5
                   && contrastRatio(darkTheme.input.selectionText,
                                    darkTheme.input.selectionBackground)
                          >= 4.5);
    expectTrue("status text remains readable in both themes",
               contrastRatio(lightTheme.statusBar.infoText,
                             lightTheme.statusBar.infoBackground) >= 4.5
                   && contrastRatio(lightTheme.statusBar.successText,
                                    lightTheme.statusBar.successBackground)
                          >= 4.5
                   && contrastRatio(lightTheme.statusBar.warningText,
                                    lightTheme.statusBar.warningBackground)
                          >= 4.5
                   && contrastRatio(lightTheme.statusBar.errorText,
                                    lightTheme.statusBar.errorBackground)
                          >= 4.5
                   && contrastRatio(darkTheme.statusBar.infoText,
                                    darkTheme.statusBar.infoBackground) >= 4.5
                   && contrastRatio(darkTheme.statusBar.successText,
                                    darkTheme.statusBar.successBackground)
                          >= 4.5
                   && contrastRatio(darkTheme.statusBar.warningText,
                                    darkTheme.statusBar.warningBackground)
                          >= 4.5
                   && contrastRatio(darkTheme.statusBar.errorText,
                                    darkTheme.statusBar.errorBackground)
                          >= 4.5);
    expectTrue("checked controls retain a visible focus ring",
               contrastRatio(lightTheme.focus.ringOnAccent,
                             lightTheme.button.backgroundChecked) >= 3.0
                   && contrastRatio(darkTheme.focus.ringOnAccent,
                                    darkTheme.button.backgroundChecked) >= 3.0);
    expectTrue("semantic warning markers remain visible",
               contrastRatio(lightTheme.statusBar.warningText,
                             lightTheme.surface.stale) >= 3.0
                   && contrastRatio(lightTheme.statusBar.warningText,
                                    lightTheme.statusBar.warningBackground)
                          >= 3.0
                   && contrastRatio(lightTheme.statusBar.errorText,
                                    lightTheme.statusBar.errorBackground)
                          >= 3.0
                   && contrastRatio(darkTheme.statusBar.warningText,
                                    darkTheme.surface.stale) >= 3.0
                   && contrastRatio(darkTheme.statusBar.warningText,
                                    darkTheme.statusBar.warningBackground)
                          >= 3.0
                   && contrastRatio(darkTheme.statusBar.errorText,
                                    darkTheme.statusBar.errorBackground)
                          >= 3.0);
    expectTrue("semantic role colors and fills are theme-specific",
               InsightVisualStyle::roleColor(
                   InsightVisualRole::Write,
                   ThemeMode::Light)
                       != InsightVisualStyle::roleColor(
                              InsightVisualRole::Write,
                              ThemeMode::Dark)
                   && InsightVisualStyle::roleFillColor(
                          InsightVisualRole::Read,
                          ThemeMode::Light)
                          != InsightVisualStyle::roleFillColor(
                                 InsightVisualRole::Read,
                                 ThemeMode::Dark));
    expectTrue("editor semantic tokens are centralized and theme-specific",
               lightTheme.editorSemantic.moduleInterface.isValid()
                   && lightTheme.editorSemantic.actualSignal.isValid()
                   && lightTheme.editorSemantic.inactiveBackground.isValid()
                   && darkTheme.editorSemantic.moduleInterface.isValid()
                   && darkTheme.editorSemantic.actualSignal.isValid()
                   && darkTheme.editorSemantic.inactiveBackground.isValid()
                   && lightTheme.editorSemantic.moduleInterface
                          != darkTheme.editorSemantic.moduleInterface
                   && lightTheme.editorSemantic.inactiveBackground
                          != darkTheme.editorSemantic.inactiveBackground);
    expectTrue("editor semantic foregrounds remain readable in both themes",
               contrastRatio(
                   lightTheme.editorSemantic.moduleInterface,
                   lightTheme.panelBackground) >= 3.0
                   && contrastRatio(
                          lightTheme.editorSemantic.actualSignal,
                          lightTheme.panelBackground) >= 3.0
                   && contrastRatio(
                          darkTheme.editorSemantic.moduleInterface,
                          darkTheme.panelBackground) >= 3.0
                   && contrastRatio(
                          darkTheme.editorSemantic.actualSignal,
                          darkTheme.panelBackground) >= 3.0);
    expectTrue("diagnostic underline tokens are centralized and theme-specific",
               lightTheme.syntax.errorUnderline.isValid()
                   && lightTheme.syntax.warningUnderline.isValid()
                   && darkTheme.syntax.errorUnderline.isValid()
                   && darkTheme.syntax.warningUnderline.isValid()
                   && lightTheme.syntax.errorUnderline
                          != lightTheme.syntax.warningUnderline
                   && darkTheme.syntax.errorUnderline
                          != darkTheme.syntax.warningUnderline
                   && lightTheme.syntax.errorUnderline
                          != darkTheme.syntax.errorUnderline
                   && lightTheme.syntax.warningUnderline
                          != darkTheme.syntax.warningUnderline);
    expectTrue("heat colors are theme-specific",
               InsightVisualStyle::heatIntensityColor(
                   0.0, ThemeMode::Light)
                   != InsightVisualStyle::heatIntensityColor(
                          0.0, ThemeMode::Dark));

    int themeSignalCount = 0;
    int themeAboutToChangeCount = 0;
    ThemeMode aboutPreviousMode = ThemeMode::Light;
    ThemeMode aboutNextMode = ThemeMode::Light;
    bool aboutSignalObservedOldManagerMode = false;
    QObject::connect(
        &themeManager,
        &ApplicationThemeManager::themeAboutToChange,
        &app,
        [&](ThemeMode previousMode, ThemeMode nextMode) {
            ++themeAboutToChangeCount;
            aboutPreviousMode = previousMode;
            aboutNextMode = nextMode;
            aboutSignalObservedOldManagerMode =
                themeManager.mode() == previousMode;
        });
    QObject::connect(
        &themeManager,
        &ApplicationThemeManager::themeChanged,
        &app,
        [&themeSignalCount](ThemeMode) {
            ++themeSignalCount;
        });
    themeManager.setMode(ThemeMode::Light);
    expectTrue("repeating current theme emits no signal",
               themeSignalCount == 0
                   && themeAboutToChangeCount == 0);
    themeManager.setMode(ThemeMode::Dark);
    expectTrue("dark switch updates manager and emits once",
               themeSignalCount == 1
                   && themeAboutToChangeCount == 1
                   && aboutPreviousMode == ThemeMode::Light
                   && aboutNextMode == ThemeMode::Dark
                   && aboutSignalObservedOldManagerMode
                   && themeManager.mode() == ThemeMode::Dark
                   && &InsightVisualStyle::theme() == &darkTheme
                   && &themeManager.theme() == &darkTheme);
    expectTrue("dark switch applies application palette",
               app.palette().color(QPalette::Window)
                       == darkTheme.appBackground
                   && app.palette().color(QPalette::Base)
                          == darkTheme.input.background
                   && app.palette().color(QPalette::Text)
                          == darkTheme.input.text
                   && app.palette().color(QPalette::HighlightedText)
                          == darkTheme.input.selectionText);
    const QString darkApplicationQss = app.styleSheet();
    expectTrue("application qss covers popup dialog and form surfaces",
               darkApplicationQss.contains(QStringLiteral("QToolTip"))
                   && darkApplicationQss.contains(QStringLiteral("QDialog"))
                   && darkApplicationQss.contains(QStringLiteral("QCheckBox"))
                   && darkApplicationQss.contains(QStringLiteral("QRadioButton"))
                   && darkApplicationQss.contains(QStringLiteral("QGroupBox"))
                   && darkApplicationQss.contains(
                       QStringLiteral("QComboBox QAbstractItemView"))
                   && darkApplicationQss.contains(QStringLiteral(":disabled"))
                   && darkApplicationQss.contains(
                       QStringLiteral("selection-background-color")));
    themeManager.setMode(ThemeMode::Dark);
    expectTrue("repeating dark theme emits no additional signal",
               themeSignalCount == 1
                   && themeAboutToChangeCount == 1);
    themeManager.setMode(ThemeMode::Light);
    expectTrue("light switch restores palette and emits once",
               themeSignalCount == 2
                   && themeAboutToChangeCount == 2
                   && aboutPreviousMode == ThemeMode::Dark
                   && aboutNextMode == ThemeMode::Light
                   && aboutSignalObservedOldManagerMode
                   && themeManager.mode() == ThemeMode::Light
                   && app.palette().color(QPalette::Window)
                          == lightTheme.appBackground
                   && &InsightVisualStyle::theme() == &lightTheme);

    const InsightTheme theme = InsightVisualStyle::theme();
    expectTrue("theme background valid", theme.appBackground.isValid());
    expectTrue("theme border valid", theme.border.isValid());
    expectTrue("theme accent distinct from warning",
               theme.accent != theme.warning);
    expectTrue("shell menu tokens valid",
               theme.menu.background.isValid()
                   && theme.menu.itemHoverBackground.isValid());
    expectTrue("tab tokens valid",
               theme.tab.tabBackgroundSelected.isValid()
                   && theme.tab.borderSelected.isValid());
    expectTrue("status tokens valid",
               theme.statusBar.successBackground.isValid()
                   && theme.statusBar.warningBorder.isValid());
    expectTrue("graph tokens valid",
               theme.graph.nodeSelectedBorder.isValid()
                   && theme.graph.edgeSelected.isValid());

    expectTrue("write role maps to semantic color",
               InsightVisualStyle::roleColor(InsightVisualRole::Write)
                   == InsightVisualStyle::roleColor(QStringLiteral("write")));
    expectTrue("read role maps to semantic color",
               InsightVisualStyle::roleColor(InsightVisualRole::Read)
                   == InsightVisualStyle::roleColor(QStringLiteral("read")));
    expectTrue("unknown role returns valid color",
               InsightVisualStyle::roleColor(QStringLiteral("future-role"))
                   .isValid());

    const QColor cold = InsightVisualStyle::heatIntensityColor(0.0);
    const QColor warm = InsightVisualStyle::heatIntensityColor(0.5);
    const QColor hot = InsightVisualStyle::heatIntensityColor(1.0);
    expectTrue("heat colors are valid",
               cold.isValid() && warm.isValid() && hot.isValid());
    expectTrue("heat colors vary by intensity", cold != warm && warm != hot);

    QLabel title;
    title.setObjectName(QStringLiteral("titleProbe"));
    InsightVisualStyle::applyTitleLabel(&title);
    expectTrue("title helper sets stable height", title.minimumHeight() >= 30);
    expectTrue("title helper emits scoped qss",
               title.styleSheet().contains(QStringLiteral("QLabel#titleProbe")));

    QLineEdit search;
    search.setObjectName(QStringLiteral("searchProbe"));
    InsightVisualStyle::applySearchField(&search);
    expectTrue("search helper sets stable width",
               search.minimumWidth() >= 180);
    expectTrue("search helper emits focus border",
               search.styleSheet().contains(QStringLiteral(":focus")));

    QPushButton toolbarButton;
    toolbarButton.setObjectName(QStringLiteral("toolbarButtonProbe"));
    InsightVisualStyle::applyToolbarButton(&toolbarButton);
    expectTrue("toolbar button helper sets stable height",
               toolbarButton.minimumHeight() >= 28);
    expectTrue("toolbar button helper styles checked state",
               toolbarButton.styleSheet().contains(QStringLiteral(":checked")));

    QLabel secondaryLabel;
    secondaryLabel.setObjectName(QStringLiteral("secondaryLabelProbe"));
    InsightVisualStyle::applyLabel(&secondaryLabel);
    QWidget globalControl;
    globalControl.setObjectName(QStringLiteral("globalControlProbe"));
    InsightVisualStyle::applyGlobalControlPanel(&globalControl);
    QWidget sideInspector;
    sideInspector.setObjectName(QStringLiteral("sideInspectorProbe"));
    InsightVisualStyle::applySideInspector(&sideInspector);

    const QString lightTitleStyle = title.styleSheet();
    const QString lightSearchStyle = search.styleSheet();
    const QString lightToolbarStyle = toolbarButton.styleSheet();
    const QString lightLabelStyle = secondaryLabel.styleSheet();
    const QString lightGlobalControlStyle = globalControl.styleSheet();
    const QString lightSideInspectorStyle = sideInspector.styleSheet();
    themeManager.setMode(ThemeMode::Dark);
    expectTrue("registered local style helpers refresh for dark theme",
               title.styleSheet() != lightTitleStyle
                   && search.styleSheet() != lightSearchStyle
                   && toolbarButton.styleSheet() != lightToolbarStyle
                   && secondaryLabel.styleSheet() != lightLabelStyle
                   && globalControl.styleSheet()
                          != lightGlobalControlStyle
                   && sideInspector.styleSheet()
                          != lightSideInspectorStyle
                   && title.styleSheet().contains(
                       darkTheme.panelSubtle.name()));
    themeManager.setMode(ThemeMode::Light);
    expectTrue("registered local style helpers restore light theme",
               title.styleSheet() == lightTitleStyle
                   && search.styleSheet() == lightSearchStyle
                   && toolbarButton.styleSheet() == lightToolbarStyle
                   && secondaryLabel.styleSheet() == lightLabelStyle
                   && globalControl.styleSheet()
                          == lightGlobalControlStyle
                   && sideInspector.styleSheet()
                          == lightSideInspectorStyle);

    expectTrue("application qss includes shell widgets",
               InsightVisualStyle::applicationStyleSheet()
                   .contains(QStringLiteral("QMenuBar"))
                   && InsightVisualStyle::applicationStyleSheet()
                          .contains(QStringLiteral("QDockWidget")));
    expectTrue("application qss includes common shell controls",
               InsightVisualStyle::applicationStyleSheet()
                   .contains(QStringLiteral("QComboBox"))
                   && InsightVisualStyle::applicationStyleSheet()
                          .contains(QStringLiteral("QTableWidget"))
                   && InsightVisualStyle::applicationStyleSheet()
                          .contains(QStringLiteral("QPlainTextEdit")));

    QWidget editorShell;
    editorShell.resize(720, 260);
    editorShell.setStyleSheet(
        InsightVisualStyle::applicationStyleSheet());
    MyCodeEditor codeEditor(&editorShell);
    codeEditor.setGeometry(editorShell.rect());
    codeEditor.setLineWrapMode(QPlainTextEdit::NoWrap);
    codeEditor.setPlainText(QStringLiteral(
        "01234567890123456789012345678901234567890123456789\n"
        "line 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\n"));
    editorShell.show();
    QApplication::processEvents();

    QWidget* gutter = codeEditor.findChild<QWidget*>(
        QStringLiteral("editorLineNumberGutter"));
    const auto viewportLeft = [&codeEditor]() {
        return codeEditor.viewport()->mapTo(
            &codeEditor, QPoint(0, 0)).x();
    };
    const auto firstColumnLeft = [&codeEditor, &viewportLeft]() {
        QTextCursor first(codeEditor.document()->firstBlock());
        return viewportLeft() + codeEditor.cursorRect(first).left();
    };
    const int oneDigitGutterWidth = gutter ? gutter->width() : -1;
    const int oneDigitViewportLeft = viewportLeft();
    const int oneDigitFirstColumnLeft = firstColumnLeft();

    expectTrue("code editor opts into its scoped surface style",
               codeEditor.property("codeEditorSurface").toBool());
    expectTrue("code editor surface has no generic frame or padding",
               codeEditor.frameWidth() == 0
                   && codeEditor.contentsRect() == codeEditor.rect());
    expectTrue("gutter and viewport share an exact boundary",
               gutter
                   && gutter->geometry().top()
                          == codeEditor.contentsRect().top()
                   && gutter->geometry().left()
                          == codeEditor.contentsRect().left()
                   && gutter->geometry().height()
                          == codeEditor.contentsRect().height()
                   && gutter->geometry().right() + 1
                          == oneDigitViewportLeft);

    QString manyLines;
    manyLines.reserve(24000);
    manyLines.append(QStringLiteral(
        "01234567890123456789012345678901234567890123456789\n"));
    for (int line = 1; line < 1200; ++line)
        manyLines.append(QStringLiteral("line %1\n").arg(line + 1));
    codeEditor.setPlainText(manyLines);
    QApplication::processEvents();
    expectTrue("line-number digit growth expands only the numeric lane",
               gutter
                   && gutter->width() > oneDigitGutterWidth
                   && viewportLeft() - oneDigitViewportLeft == gutter->width() - oneDigitGutterWidth
                   && firstColumnLeft() - oneDigitFirstColumnLeft == gutter->width() - oneDigitGutterWidth);

    const int fourDigitWidth = gutter->width();
    const int fourDigitLeft = viewportLeft();
    QFont zoomed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    zoomed.setPointSize(32);
    codeEditor.setFont(zoomed);
    QApplication::processEvents();
    expectTrue("font and zoom changes do not move the editing origin",
               gutter
                   && gutter->width() == fourDigitWidth
                   && viewportLeft() == fourDigitLeft);
    const int lineNumberLaneWidth = gutter
        ? gutter->width() - 18 - 4 - 16
        : -1;
    const QString widestVisibleLine =
        QString::number(codeEditor.blockCount());
    expectTrue("multi-digit line numbers remain visible at maximum zoom",
               gutter
                   && lineNumberLaneWidth > 0
                   && QFontMetrics(gutter->font())
                              .horizontalAdvance(widestVisibleLine)
                          <= lineNumberLaneWidth
                   && gutter->font().pointSizeF() <= 10.0);

    const int boundaryBeforeScroll = viewportLeft();
    QTextCursor firstBeforeScroll(codeEditor.document()->firstBlock());
    const int firstColumnBeforeScroll =
        codeEditor.cursorRect(firstBeforeScroll).left();
    const int scrollAmount = qMin(
        31, codeEditor.horizontalScrollBar()->maximum());
    codeEditor.horizontalScrollBar()->setValue(scrollAmount);
    QApplication::processEvents();
    QTextCursor firstAfterScroll(codeEditor.document()->firstBlock());
    expectTrue("horizontal scrolling preserves the gutter boundary",
               scrollAmount > 0
                   && viewportLeft() == boundaryBeforeScroll
                   && gutter
                   && gutter->geometry().right() + 1
                          == viewportLeft()
                   && firstColumnBeforeScroll
                          - codeEditor.cursorRect(firstAfterScroll).left()
                          == scrollAmount);
    expectTrue("tab bar qss is scoped",
               InsightVisualStyle::tabBarStyleSheet(
                   QStringLiteral("mainEditorTabBar"))
                   .contains(QStringLiteral("QTabBar#mainEditorTabBar")));
    expectTrue("package tools qss is scoped",
               InsightVisualStyle::packageToolsBarStyleSheet(
                   QStringLiteral("packageToolsBar"))
                   .contains(QStringLiteral("QWidget#packageToolsBar")));
    expectTrue("status chip qss is scoped",
               InsightVisualStyle::statusChipStyleSheet(
                   InsightStatusTone::Warning,
                   QStringLiteral("editorModeChip"))
                   .contains(QStringLiteral("QLabel#editorModeChip")));
    expectTrue("dock attention qss is scoped",
               InsightVisualStyle::dockAttentionStyleSheet(
                   QStringLiteral("FoldShelfDock"))
                   .contains(QStringLiteral("QDockWidget#FoldShelfDock")));
    expectTrue("global control qss is scoped",
               InsightVisualStyle::globalControlPanelStyleSheet(
                   QStringLiteral("globalControlPanel"))
                   .contains(QStringLiteral("QFrame#globalControlPanel")));
    expectTrue("fold shelf active qss is scoped",
               InsightVisualStyle::foldShelfActiveStyleSheet(
                   QStringLiteral("foldBlockShelfPanel"))
                   .contains(QStringLiteral("QWidget#foldBlockShelfPanel")));
    expectTrue("graph view qss is scoped",
               InsightVisualStyle::graphViewStyleSheet(
                   QStringLiteral("signalKernelGraphView"))
                   .contains(
                       QStringLiteral("QGraphicsView#signalKernelGraphView")));

    QGraphicsScene graphScene;
    graphScene.setSceneRect(0, 0, 200, 120);
    InsightGraphView graphView(&graphScene);
    graphView.setObjectName(QStringLiteral("graphViewProbe"));
    graphView.applyInsightGraphStyle();
    graphView.setZoomRange(0.5, 2.0);
    graphView.zoomBy(1.5);
    expectTrue("graph view zoom helper changes transform",
               graphView.currentZoom() > 1.0);
    graphView.resetView();
    expectTrue("graph view reset helper restores transform",
               qAbs(graphView.currentZoom() - 1.0) < 0.001);
    expectTrue("graph view uses theme qss",
               graphView.styleSheet().contains(
                   QStringLiteral("QGraphicsView#graphViewProbe")));

    const QColor lightGraphBackground =
        graphView.backgroundBrush().color();
    const QString lightGraphStyle = graphView.styleSheet();
    graphView.zoomBy(1.25);
    const qreal zoomBeforeThemeSwitch = graphView.currentZoom();
    themeManager.setMode(ThemeMode::Dark);
    expectTrue("graph view refreshes background and qss in dark theme",
               graphView.backgroundBrush().color()
                       == darkTheme.canvasBackground
                   && graphView.backgroundBrush().color()
                          != lightGraphBackground
                   && graphView.styleSheet() != lightGraphStyle
                   && graphView.styleSheet().contains(
                       darkTheme.graph.background.name()));
    expectTrue("graph theme refresh preserves transform",
               qAbs(graphView.currentZoom() - zoomBeforeThemeSwitch)
                   < 0.001);
    themeManager.setMode(ThemeMode::Light);
    expectTrue("graph view restores light theme",
               graphView.backgroundBrush().color()
                       == lightTheme.canvasBackground
                   && graphView.styleSheet() == lightGraphStyle);

    const QString highlightText = QStringLiteral(
        "module theme_probe; // theme comment\nendmodule\n");
    TSDocument syntaxDocument;
    syntaxDocument.setText(highlightText);
    QTextDocument textDocument(highlightText);
    MyHighlighter highlighter(&textDocument, &syntaxDocument);
    highlighter.rehighlight();
    const int keywordPosition =
        highlightText.indexOf(QStringLiteral("module"));
    const int commentPosition =
        highlightText.indexOf(QStringLiteral("theme comment"));
    const QColor lightKeyword =
        formatColorAt(textDocument, keywordPosition);
    const QColor lightComment =
        formatColorAt(textDocument, commentPosition);
    expectTrue("highlighter starts with light syntax tokens",
               lightKeyword == lightTheme.syntax.keyword
                   && lightComment == lightTheme.syntax.comment);
    themeManager.setMode(ThemeMode::Dark);
    const QColor darkKeyword =
        formatColorAt(textDocument, keywordPosition);
    const QColor darkComment =
        formatColorAt(textDocument, commentPosition);
    expectTrue("highlighter rehighlights with dark syntax tokens",
               darkKeyword == darkTheme.syntax.keyword
                   && darkComment == darkTheme.syntax.comment
                   && darkKeyword != lightKeyword
                   && darkComment != lightComment);
    themeManager.setMode(ThemeMode::Light);
    expectTrue("highlighter restores light syntax tokens",
               formatColorAt(textDocument, keywordPosition)
                       == lightTheme.syntax.keyword
                   && formatColorAt(textDocument, commentPosition)
                          == lightTheme.syntax.comment);
    graphView.resetView();

    QCheckBox segment;
    segment.setObjectName(QStringLiteral("segmentProbe"));
    InsightVisualStyle::applySegmentedCheckBox(&segment);
    expectTrue("segmented helper sets stable height",
               segment.minimumHeight() >= 28);
    expectTrue("segmented helper styles indicator",
               segment.styleSheet().contains(QStringLiteral("::indicator")));
    const QString lightSegmentStyle = segment.styleSheet();
    themeManager.setMode(ThemeMode::Dark);
    expectTrue("segmented helper refreshes for dark theme",
               segment.styleSheet() != lightSegmentStyle
                   && segment.styleSheet().contains(
                       darkTheme.itemView.hoverBackground.name()));
    themeManager.setMode(ThemeMode::Light);
    expectTrue("segmented helper restores light theme",
               segment.styleSheet() == lightSegmentStyle);

    NavigationWidget navigation;
    const QIcon lightInstanceIcon =
        navigation.symbolIconForTest(
            SymbolOutlineIconKind::Instance);
    const QImage lightInstancePixels =
        lightInstanceIcon.pixmap(16, 16).toImage();
    expectTrue("navigation icon cache stores the current theme variant",
               navigation.iconCacheEntryCountForTest() == 1);
    themeManager.setMode(ThemeMode::Dark);
    expectTrue("navigation theme change invalidates stale icon caches",
               navigation.iconCacheEntryCountForTest() == 0);
    const QIcon darkInstanceIcon =
        navigation.symbolIconForTest(
            SymbolOutlineIconKind::Instance);
    const QImage darkInstancePixels =
        darkInstanceIcon.pixmap(16, 16).toImage();
    expectTrue("navigation self-painted icon pixels consume Dark tokens",
               lightInstanceIcon.cacheKey()
                       != darkInstanceIcon.cacheKey()
                   && lightInstancePixels
                          != darkInstancePixels
                   && lightInstancePixels.pixelColor(6, 2)
                          != darkInstancePixels.pixelColor(6, 2));
    themeManager.setMode(ThemeMode::Light);
    expectTrue("navigation Light-Dark-Light regenerates the original icon pixels",
               navigation.iconCacheEntryCountForTest() == 0
                   && navigation.symbolIconForTest(
                          SymbolOutlineIconKind::Instance)
                          .pixmap(16, 16).toImage()
                          == lightInstancePixels);

    std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
