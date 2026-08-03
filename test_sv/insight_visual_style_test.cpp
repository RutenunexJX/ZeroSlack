#include "insightgraphview.h"
#include "insightvisualstyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

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
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

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

    QCheckBox segment;
    segment.setObjectName(QStringLiteral("segmentProbe"));
    InsightVisualStyle::applySegmentedCheckBox(&segment);
    expectTrue("segmented helper sets stable height",
               segment.minimumHeight() >= 28);
    expectTrue("segmented helper styles indicator",
               segment.styleSheet().contains(QStringLiteral("::indicator")));

    std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
