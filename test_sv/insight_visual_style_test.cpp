#include "insightvisualstyle.h"

#include <QApplication>
#include <QCheckBox>
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
