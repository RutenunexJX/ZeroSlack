#include "insightvisualstyle.h"
#include "insightcontrolstyle.h"
#include "uitypography.h"
#include "uicontrols.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QWidget>

#include <algorithm>

namespace {
QColor color(const char* hex)
{
    return QColor(QString::fromLatin1(hex));
}

QColor mix(const QColor& from, const QColor& to, double amount)
{
    const double t = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QString objectSelector(const QString& typeName, const QString& objectName)
{
    if (objectName.isEmpty())
        return typeName;
    return QStringLiteral("%1#%2").arg(typeName, objectName);
}

QString floatingBackgroundRule(const QString& typeName, const QString& objectName)
{
    // Keep this rule in local styles too: a child's stylesheet takes precedence
    // over its host. Ancestry lets docking restore the normal surface directly.
    return QStringLiteral("#contextFloatingWindow %1 { background: transparent; }")
        .arg(objectSelector(typeName, objectName));
}

constexpr const char* kThemeHelperProperty =
    "_zeroslackInsightThemeHelper";
constexpr const char* kThemeHelperConnectedProperty =
    "_zeroslackInsightThemeHelperConnected";

int minimumControlHeight(const QWidget* widget)
{
    const int inset = qMax(widget->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, widget),
                           widget->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, widget));
    return qMax(widget->minimumSizeHint().height(), widget->fontMetrics().height() + inset * 2);
}

void refreshRegisteredWidget(QWidget* widget)
{
    if (!widget)
        return;
    const QString helper =
        widget->property(kThemeHelperProperty).toString();
    if (helper == QStringLiteral("panel")) {
        widget->setStyleSheet(
            InsightVisualStyle::panelStyleSheet(widget->objectName()));
    } else if (helper == QStringLiteral("title")) {
        widget->setStyleSheet(
            InsightVisualStyle::titleBarStyleSheet(widget->objectName()));
    } else if (helper == QStringLiteral("label")) {
        widget->setStyleSheet(
            InsightVisualStyle::labelStyleSheet(widget->objectName()));
    } else if (helper == QStringLiteral("strongLabel")) {
        widget->setStyleSheet(
            InsightVisualStyle::labelStyleSheet(
                widget->objectName(), true));
    } else if (helper == QStringLiteral("search")) {
        if (widget->property("zeroslackElaControl").toBool()) return;
        widget->setStyleSheet(
            InsightVisualStyle::compactSearchFieldStyleSheet(
                widget->objectName()));
    } else if (helper == QStringLiteral("globalControlPanel")) {
        widget->setStyleSheet(
            InsightVisualStyle::globalControlPanelStyleSheet(
                widget->objectName()));
    } else if (helper == QStringLiteral("sideInspector")) {
        widget->setStyleSheet(
            InsightVisualStyle::sideInspectorStyleSheet(
                widget->objectName()));
    }
}

void registerThemedWidget(QWidget* widget, const QString& helper)
{
    if (!widget)
        return;
    widget->setProperty(kThemeHelperProperty, helper);
    if (!widget->property(kThemeHelperConnectedProperty).toBool()) {
        widget->setProperty(kThemeHelperConnectedProperty, true);
        QObject::connect(
            &ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            widget,
            [widget](ThemeMode) {
                refreshRegisteredWidget(widget);
                widget->update();
            });
    }
    refreshRegisteredWidget(widget);
}

void applyControlRole(QWidget* widget, const QString& role)
{
    if (!widget)
        return;
    widget->setProperty(kThemeHelperProperty, role);
    UiControls::refreshRole(widget);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->setMinimumHeight(minimumControlHeight(widget));
    widget->updateGeometry();
    widget->update();
}
}

namespace {
InsightTheme buildLightTheme()
{
    InsightTheme theme;
    theme.appBackground = color("#eef1f5");
    theme.canvasBackground = color("#f6f8fb");
    theme.panelBackground = color("#ffffff");
    theme.panelSubtle = color("#f8fafc");
    theme.border = color("#d9e0ea");
    theme.borderStrong = color("#aeb8c8");
    theme.textPrimary = color("#242b39");
    theme.textSecondary = color("#515d6d");
    theme.textMuted = color("#7a8797");
    theme.accent = color("#2563eb");
    theme.selected = color("#db2777");
    theme.hover = color("#0f766e");
    theme.warning = color("#b45309");
    theme.splitterHandle = color("#cbd5e1");
    theme.toolbarBackground = color("#f8fafc");
    theme.surface.canvas = theme.canvasBackground;
    theme.surface.panel = theme.panelBackground;
    theme.surface.raised = color("#ffffff");
    theme.surface.overlay = color("#f8fafc");
    theme.surface.empty = color("#f8fafc");
    theme.surface.loading = color("#eff6ff");
    theme.surface.stale = color("#fffbeb");
    theme.focus.ring = color("#2563eb");
    theme.focus.ringOnAccent = color("#ffffff");

    theme.menu.background = theme.panelBackground;
    theme.menu.itemHoverBackground = color("#eef4ff");
    theme.menu.text = color("#111827");
    theme.menu.itemHoverText = color("#1d4ed8");
    theme.menu.border = color("#d8e1ec");

    theme.button.background = theme.panelBackground;
    theme.button.backgroundHover = color("#eef4ff");
    theme.button.backgroundPressed = color("#dbeafe");
    theme.button.backgroundChecked = theme.accent;
    theme.button.text = color("#334155");
    theme.button.textHover = color("#1d4ed8");
    theme.button.textChecked = color("#ffffff");
    theme.button.textDisabled = color("#94a3b8");
    theme.button.border = color("#d8e1ec");
    theme.button.borderHover = color("#bfdbfe");
    theme.button.borderChecked = theme.accent;

    theme.input.background = theme.panelBackground;
    theme.input.text = theme.textPrimary;
    theme.input.border = color("#d8e1ec");
    theme.input.focusBorder = theme.accent;
    theme.input.selectionBackground = color("#bfdbfe");
    theme.input.selectionText = theme.textPrimary;

    theme.tab.barBackground = color("#f4f7fb");
    theme.tab.tabBackground = color("#f3f6fa");
    theme.tab.tabBackgroundHover = color("#eef4ff");
    theme.tab.tabBackgroundSelected = theme.panelBackground;
    theme.tab.text = color("#475569");
    theme.tab.textHover = color("#1e40af");
    theme.tab.textSelected = color("#1d4ed8");
    theme.tab.border = color("#d8e1ec");
    theme.tab.borderSelected = color("#b9cff4");


    theme.statusBar.background = theme.panelBackground;
    theme.statusBar.text = color("#334155");
    theme.statusBar.border = color("#d8e1ec");
    theme.statusBar.infoText = color("#1e3a8a");
    theme.statusBar.infoBackground = color("#dbeafe");
    theme.statusBar.infoBorder = color("#93c5fd");
    theme.statusBar.successText = color("#064e3b");
    theme.statusBar.successBackground = color("#d1fae5");
    theme.statusBar.successBorder = color("#34d399");
    theme.statusBar.warningText = color("#78350f");
    theme.statusBar.warningBackground = color("#fef3c7");
    theme.statusBar.warningBorder = color("#f59e0b");
    theme.statusBar.errorText = color("#7f1d1d");
    theme.statusBar.errorBackground = color("#fee2e2");
    theme.statusBar.errorBorder = color("#f87171");

    theme.dock.background = color("#f4f7fb");
    theme.dock.titleBackground = theme.panelBackground;
    theme.dock.border = color("#d8e1ec");
    theme.dock.titleBorder = color("#d8e1ec");
    theme.dock.text = color("#1f2937");

    theme.itemView.background = theme.panelBackground;
    theme.itemView.alternateBackground = color("#f8fafc");
    theme.itemView.headerBackground = color("#f8fafc");
    theme.itemView.border = color("#e2e8f0");
    theme.itemView.headerBorder = color("#e2e8f0");
    theme.itemView.text = color("#1f2937");
    theme.itemView.headerText = color("#475569");
    theme.itemView.selectedBackground = color("#dbeafe");
    theme.itemView.hoverBackground = color("#eef4ff");

    theme.graph.background = theme.canvasBackground;
    theme.graph.gridLine = color("#e2e8f0");
    theme.graph.nodeFill = theme.panelBackground;
    theme.graph.nodeBorder = theme.borderStrong;
    theme.graph.nodeHoverFill = color("#eef4ff");
    theme.graph.nodeHoverBorder = theme.hover;
    theme.graph.nodeSelectedFill = color("#fdf2f8");
    theme.graph.nodeSelectedBorder = theme.selected;
    theme.graph.edge = color("#64748b");
    theme.graph.edgeHover = theme.hover;
    theme.graph.edgeSelected = theme.selected;
    theme.graph.selectionFill = color("#dbeafe");
    theme.graph.selectionBorder = theme.accent;

    theme.syntax.keyword = color("#C678DD");
    theme.syntax.comment = color("#7F848E");
    theme.syntax.number = color("#D19A66");
    theme.syntax.string = color("#98C379");
    theme.syntax.errorUnderline = color("#EF4444");
    theme.syntax.warningUnderline = color("#FBBF24");
    theme.syntax.structuralPair = color("#EAB308");

    theme.semantic.write = color("#b45309");
    theme.semantic.read = color("#15803d");
    theme.semantic.port = color("#2563eb");
    theme.semantic.condition = color("#7c3aed");
    theme.semantic.caseRole = color("#c026d3");
    theme.semantic.timing = color("#0284c7");
    theme.semantic.unknown = color("#64748b");
    theme.semantic.kernel = color("#1d4ed8");
    theme.semantic.data = color("#16a34a");
    theme.semantic.writeFill = color("#fff7ed");
    theme.semantic.readFill = color("#ecfdf3");
    theme.semantic.portFill = color("#eff6ff");
    theme.semantic.conditionFill = color("#f5f3ff");
    theme.semantic.caseFill = color("#fdf4ff");
    theme.semantic.timingFill = color("#e0f2fe");
    theme.semantic.unknownFill = color("#f8fafc");
    theme.semantic.kernelFill = color("#eff6ff");
    theme.semantic.dataFill = color("#dcfce7");
    theme.semantic.heatLow = color("#f8fafc");
    theme.semantic.heatMid = color("#facc15");
    theme.semantic.heatHigh = color("#ef4444");
    theme.editorSemantic.moduleInterface = color("#005CC5");
    theme.editorSemantic.packageClassType = color("#007C89");
    theme.editorSemantic.instanceName = color("#8A5A00");
    theme.editorSemantic.formalPort = color("#22863A");
    theme.editorSemantic.modulePort = color("#0366D6");
    theme.editorSemantic.actualSignal = color("#B05A00");
    theme.editorSemantic.parameter = color("#D73A49");
    theme.editorSemantic.enumValue = color("#795E26");
    theme.editorSemantic.typeAlias = color("#00796B");
    theme.editorSemantic.macro = color("#735C0F");
    theme.editorSemantic.systemTask = color("#007C89");
    theme.editorSemantic.inactiveText = color("#9CA3AF");
    theme.editorSemantic.inactiveBackground = color("#F3F4F6");
    return theme;
}

InsightTheme buildDarkTheme()
{
    InsightTheme theme;
    theme.appBackground = color("#0b1120");
    theme.canvasBackground = color("#0b1220");
    theme.panelBackground = color("#111827");
    theme.panelSubtle = color("#172033");
    theme.border = color("#334155");
    theme.borderStrong = color("#64748b");
    theme.textPrimary = color("#dce2ec");
    theme.textSecondary = color("#b1bac9");
    theme.textMuted = color("#94a3b8");
    theme.accent = color("#60a5fa");
    theme.selected = color("#f472b6");
    theme.hover = color("#2dd4bf");
    theme.warning = color("#fbbf24");
    theme.splitterHandle = color("#334155");
    theme.toolbarBackground = color("#0f172a");
    theme.surface.canvas = theme.canvasBackground;
    theme.surface.panel = theme.panelBackground;
    theme.surface.raised = color("#172033");
    theme.surface.overlay = color("#1e293b");
    theme.surface.empty = color("#172033");
    theme.surface.loading = color("#172554");
    theme.surface.stale = color("#422006");
    theme.focus.ring = color("#60a5fa");
    theme.focus.ringOnAccent = color("#ffffff");

    theme.menu.background = theme.panelBackground;
    theme.menu.itemHoverBackground = color("#1e3a5f");
    theme.menu.text = theme.textPrimary;
    theme.menu.itemHoverText = color("#dbeafe");
    theme.menu.border = theme.border;

    theme.button.background = theme.panelSubtle;
    theme.button.backgroundHover = color("#1e3a5f");
    theme.button.backgroundPressed = color("#1e40af");
    theme.button.backgroundChecked = color("#2563eb");
    theme.button.text = color("#e2e8f0");
    theme.button.textHover = color("#f8fafc");
    theme.button.textChecked = color("#ffffff");
    theme.button.textDisabled = color("#64748b");
    theme.button.border = color("#475569");
    theme.button.borderHover = color("#60a5fa");
    theme.button.borderChecked = color("#60a5fa");

    theme.input.background = color("#0f172a");
    theme.input.text = color("#f8fafc");
    theme.input.border = color("#475569");
    theme.input.focusBorder = theme.accent;
    theme.input.selectionBackground = color("#1d4ed8");
    theme.input.selectionText = color("#ffffff");

    theme.tab.barBackground = color("#0f172a");
    theme.tab.tabBackground = color("#172033");
    theme.tab.tabBackgroundHover = color("#1e3a5f");
    theme.tab.tabBackgroundSelected = theme.panelBackground;
    theme.tab.text = color("#94a3b8");
    theme.tab.textHover = color("#dbeafe");
    theme.tab.textSelected = color("#bfdbfe");
    theme.tab.border = theme.border;
    theme.tab.borderSelected = color("#3b82f6");

    theme.statusBar.background = color("#0f172a");
    theme.statusBar.text = color("#e2e8f0");
    theme.statusBar.border = theme.border;
    theme.statusBar.infoText = color("#bfdbfe");
    theme.statusBar.infoBackground = color("#1e3a8a");
    theme.statusBar.infoBorder = color("#60a5fa");
    theme.statusBar.successText = color("#bbf7d0");
    theme.statusBar.successBackground = color("#14532d");
    theme.statusBar.successBorder = color("#4ade80");
    theme.statusBar.warningText = color("#fde68a");
    theme.statusBar.warningBackground = color("#713f12");
    theme.statusBar.warningBorder = color("#fbbf24");
    theme.statusBar.errorText = color("#fecaca");
    theme.statusBar.errorBackground = color("#7f1d1d");
    theme.statusBar.errorBorder = color("#f87171");

    theme.dock.background = theme.appBackground;
    theme.dock.titleBackground = theme.panelBackground;
    theme.dock.border = theme.border;
    theme.dock.titleBorder = theme.border;
    theme.dock.text = theme.textPrimary;

    theme.itemView.background = theme.panelBackground;
    theme.itemView.alternateBackground = color("#151f31");
    theme.itemView.headerBackground = color("#172033");
    theme.itemView.border = theme.border;
    theme.itemView.headerBorder = theme.border;
    theme.itemView.text = color("#e5e7eb");
    theme.itemView.headerText = color("#cbd5e1");
    theme.itemView.selectedBackground = color("#1e3a8a");
    theme.itemView.hoverBackground = color("#1e293b");

    theme.graph.background = theme.canvasBackground;
    theme.graph.gridLine = color("#25344a");
    theme.graph.nodeFill = theme.panelBackground;
    theme.graph.nodeBorder = theme.borderStrong;
    theme.graph.nodeHoverFill = color("#1e293b");
    theme.graph.nodeHoverBorder = theme.hover;
    theme.graph.nodeSelectedFill = color("#3b1630");
    theme.graph.nodeSelectedBorder = theme.selected;
    theme.graph.edge = color("#94a3b8");
    theme.graph.edgeHover = theme.hover;
    theme.graph.edgeSelected = theme.selected;
    theme.graph.selectionFill = color("#172554");
    theme.graph.selectionBorder = theme.accent;

    theme.syntax.keyword = color("#e879f9");
    theme.syntax.comment = color("#94a3b8");
    theme.syntax.number = color("#f6ad55");
    theme.syntax.string = color("#86efac");
    theme.syntax.errorUnderline = color("#f87171");
    theme.syntax.warningUnderline = color("#facc15");
    theme.syntax.structuralPair = color("#facc15");

    theme.semantic.write = color("#f59e0b");
    theme.semantic.read = color("#4ade80");
    theme.semantic.port = color("#60a5fa");
    theme.semantic.condition = color("#c084fc");
    theme.semantic.caseRole = color("#e879f9");
    theme.semantic.timing = color("#38bdf8");
    theme.semantic.unknown = color("#94a3b8");
    theme.semantic.kernel = color("#93c5fd");
    theme.semantic.data = color("#22c55e");
    theme.semantic.writeFill = color("#3b2710");
    theme.semantic.readFill = color("#143321");
    theme.semantic.portFill = color("#172554");
    theme.semantic.conditionFill = color("#2e1065");
    theme.semantic.caseFill = color("#3b0a45");
    theme.semantic.timingFill = color("#0c3447");
    theme.semantic.unknownFill = color("#1e293b");
    theme.semantic.kernelFill = color("#172554");
    theme.semantic.dataFill = color("#12351f");
    theme.semantic.heatLow = color("#172033");
    theme.semantic.heatMid = color("#ca8a04");
    theme.semantic.heatHigh = color("#ef4444");
    theme.editorSemantic.moduleInterface = color("#61AFEF");
    theme.editorSemantic.packageClassType = color("#56B6C2");
    theme.editorSemantic.instanceName = color("#E5C07B");
    theme.editorSemantic.formalPort = color("#98C379");
    theme.editorSemantic.modulePort = color("#9CDCFE");
    theme.editorSemantic.actualSignal = color("#D19A66");
    theme.editorSemantic.parameter = color("#E06C75");
    theme.editorSemantic.enumValue = color("#DCDCAA");
    theme.editorSemantic.typeAlias = color("#4EC9B0");
    theme.editorSemantic.macro = color("#D7BA7D");
    theme.editorSemantic.systemTask = color("#56B6C2");
    theme.editorSemantic.inactiveText = color("#6B7280");
    theme.editorSemantic.inactiveBackground = color("#1F2937");
    return theme;
}
}

// Catppuccin palette 1.8.0: https://github.com/catppuccin/palette (MIT).
InsightTheme buildCatppuccinTheme(int flavor)
{
    const char* baseValues[] = {"#eff1f5", "#303446", "#24273a", "#1e1e2e"};
    const QColor base = color(baseValues[flavor]);
    const char* blueValues[] = {"#1e66f5", "#8caaee", "#8aadf4", "#89b4fa"};
    const QColor blue = color(blueValues[flavor]);
    const char* greenValues[] = {"#40a02b", "#a6d189", "#a6da95", "#a6e3a1"};
    const QColor green = color(greenValues[flavor]);
    const char* mantleValues[] = {"#e6e9ef", "#292c3c", "#1e2030", "#181825"};
    const QColor mantle = color(mantleValues[flavor]);
    const char* mauveValues[] = {"#8839ef", "#ca9ee6", "#c6a0f6", "#cba6f7"};
    const QColor mauve = color(mauveValues[flavor]);
    const char* overlay0Values[] = {"#9ca0b0", "#737994", "#6e738d", "#6c7086"};
    const QColor overlay0 = color(overlay0Values[flavor]);
    const char* peachValues[] = {"#fe640b", "#ef9f76", "#f5a97f", "#fab387"};
    const QColor peach = color(peachValues[flavor]);
    const char* redValues[] = {"#d20f39", "#e78284", "#ed8796", "#f38ba8"};
    const QColor red = color(redValues[flavor]);
    const char* subtext0Values[] = {"#6c6f85", "#a5adce", "#a5adcb", "#a6adc8"};
    const QColor subtext0 = color(subtext0Values[flavor]);
    const char* subtext1Values[] = {"#5c5f77", "#b5bfe2", "#b8c0e0", "#bac2de"};
    const QColor subtext1 = color(subtext1Values[flavor]);
    const char* surface0Values[] = {"#ccd0da", "#414559", "#363a4f", "#313244"};
    const QColor surface0 = color(surface0Values[flavor]);
    const char* surface1Values[] = {"#bcc0cc", "#51576d", "#494d64", "#45475a"};
    const QColor surface1 = color(surface1Values[flavor]);
    const char* surface2Values[] = {"#acb0be", "#626880", "#5b6078", "#585b70"};
    const QColor surface2 = color(surface2Values[flavor]);
    const char* tealValues[] = {"#179299", "#81c8be", "#8bd5ca", "#94e2d5"};
    const QColor teal = color(tealValues[flavor]);
    const char* textValues[] = {"#4c4f69", "#c6d0f5", "#cad3f5", "#cdd6f4"};
    const QColor text = color(textValues[flavor]);
    const char* yellowValues[] = {"#df8e1d", "#e5c890", "#eed49f", "#f9e2af"};
    const QColor yellow = color(yellowValues[flavor]);
    InsightTheme theme;
    theme.appBackground = mantle;
    theme.canvasBackground = base;
    theme.panelBackground = base;
    theme.panelSubtle = surface0;
    theme.border = surface1;
    theme.borderStrong = overlay0;
    theme.textPrimary = text;
    theme.textSecondary = subtext1;
    theme.textMuted = subtext0;
    theme.accent = blue;
    theme.selected = mauve;
    theme.hover = teal;
    theme.warning = yellow;
    theme.splitterHandle = surface1;
    theme.toolbarBackground = mantle;
    theme.surface.canvas = base;
    theme.surface.panel = base;
    theme.surface.raised = surface0;
    theme.surface.overlay = base;
    theme.surface.empty = base;
    theme.surface.loading = mix(base, blue, 0.15);
    theme.surface.stale = mix(base, yellow, 0.15);
    theme.focus.ring = blue;
    theme.focus.ringOnAccent = base;
    theme.menu.background = base;
    theme.menu.itemHoverBackground = surface1;
    theme.menu.text = text;
    theme.menu.itemHoverText = subtext1;
    theme.menu.border = surface1;
    theme.button.background = surface0;
    theme.button.backgroundHover = surface1;
    theme.button.backgroundPressed = surface2;
    theme.button.backgroundChecked = blue;
    theme.button.text = subtext1;
    theme.button.textHover = text;
    theme.button.textChecked = base;
    theme.button.textDisabled = subtext0;
    theme.button.border = surface1;
    theme.button.borderHover = blue;
    theme.button.borderChecked = blue;
    theme.input.background = base;
    theme.input.text = text;
    theme.input.border = surface1;
    theme.input.focusBorder = blue;
    theme.input.selectionBackground = blue;
    theme.input.selectionText = base;
    theme.tab.barBackground = mantle;
    theme.tab.tabBackground = surface0;
    theme.tab.tabBackgroundHover = surface1;
    theme.tab.tabBackgroundSelected = base;
    theme.tab.text = subtext0;
    theme.tab.textHover = subtext1;
    theme.tab.textSelected = text;
    theme.tab.border = surface1;
    theme.tab.borderSelected = blue;
    theme.statusBar.background = mantle;
    theme.statusBar.text = text;
    theme.statusBar.border = surface1;
    theme.statusBar.infoText = blue;
    theme.statusBar.infoBackground = mix(base, blue, 0.15);
    theme.statusBar.infoBorder = blue;
    theme.statusBar.successText = green;
    theme.statusBar.successBackground = mix(base, green, 0.15);
    theme.statusBar.successBorder = green;
    theme.statusBar.warningText = yellow;
    theme.statusBar.warningBackground = mix(base, yellow, 0.15);
    theme.statusBar.warningBorder = yellow;
    theme.statusBar.errorText = red;
    theme.statusBar.errorBackground = mix(base, red, 0.15);
    theme.statusBar.errorBorder = red;
    theme.dock.background = mantle;
    theme.dock.titleBackground = base;
    theme.dock.border = surface1;
    theme.dock.titleBorder = surface1;
    theme.dock.text = text;
    theme.itemView.background = base;
    theme.itemView.alternateBackground = surface0;
    theme.itemView.headerBackground = surface0;
    theme.itemView.border = surface1;
    theme.itemView.headerBorder = surface1;
    theme.itemView.text = text;
    theme.itemView.headerText = subtext1;
    theme.itemView.selectedBackground = surface2;
    theme.itemView.hoverBackground = surface1;
    theme.graph.background = base;
    theme.graph.gridLine = surface1;
    theme.graph.nodeFill = base;
    theme.graph.nodeBorder = overlay0;
    theme.graph.nodeHoverFill = surface0;
    theme.graph.nodeHoverBorder = teal;
    theme.graph.nodeSelectedFill = mix(base, blue, 0.15);
    theme.graph.nodeSelectedBorder = mauve;
    theme.graph.edge = subtext0;
    theme.graph.edgeHover = teal;
    theme.graph.edgeSelected = mauve;
    theme.graph.selectionFill = mix(base, blue, 0.15);
    theme.graph.selectionBorder = blue;
    theme.syntax.keyword = mauve;
    theme.syntax.comment = subtext0;
    theme.syntax.number = peach;
    theme.syntax.string = green;
    theme.syntax.errorUnderline = red;
    theme.syntax.warningUnderline = yellow;
    theme.syntax.structuralPair = yellow;
    theme.semantic.write = peach;
    theme.semantic.read = green;
    theme.semantic.port = blue;
    theme.semantic.condition = mauve;
    theme.semantic.caseRole = mauve;
    theme.semantic.timing = teal;
    theme.semantic.unknown = subtext0;
    theme.semantic.kernel = blue;
    theme.semantic.data = green;
    theme.semantic.writeFill = mix(base, peach, 0.16);
    theme.semantic.readFill = mix(base, green, 0.16);
    theme.semantic.portFill = mix(base, blue, 0.16);
    theme.semantic.conditionFill = mix(base, mauve, 0.16);
    theme.semantic.caseFill = mix(base, mauve, 0.16);
    theme.semantic.timingFill = mix(base, teal, 0.16);
    theme.semantic.unknownFill = mix(base, subtext0, 0.16);
    theme.semantic.kernelFill = mix(base, blue, 0.16);
    theme.semantic.dataFill = mix(base, green, 0.16);
    theme.semantic.heatLow = surface0;
    theme.semantic.heatMid = yellow;
    theme.semantic.heatHigh = red;
    theme.editorSemantic.moduleInterface = blue;
    theme.editorSemantic.packageClassType = teal;
    theme.editorSemantic.instanceName = yellow;
    theme.editorSemantic.formalPort = green;
    theme.editorSemantic.modulePort = blue;
    theme.editorSemantic.actualSignal = peach;
    theme.editorSemantic.parameter = red;
    theme.editorSemantic.enumValue = yellow;
    theme.editorSemantic.typeAlias = teal;
    theme.editorSemantic.macro = yellow;
    theme.editorSemantic.systemTask = teal;
    theme.editorSemantic.inactiveText = subtext0;
    theme.editorSemantic.inactiveBackground = surface0;
    return theme;
}

const InsightTheme& InsightVisualStyle::theme()
{
    return theme(ApplicationThemeManager::instance().mode());
}

const InsightTheme& InsightVisualStyle::theme(ThemeMode mode)
{
    static const InsightTheme lightTheme = buildLightTheme();
    static const InsightTheme darkTheme = buildDarkTheme();
    static const InsightTheme catppuccin[] = {buildCatppuccinTheme(0), buildCatppuccinTheme(1),
        buildCatppuccinTheme(2), buildCatppuccinTheme(3)};
    switch (mode) {
    case ThemeMode::CatppuccinLatte: return catppuccin[0];
    case ThemeMode::CatppuccinFrappe: return catppuccin[1];
    case ThemeMode::CatppuccinMacchiato: return catppuccin[2];
    case ThemeMode::CatppuccinMocha: return catppuccin[3];
    case ThemeMode::Dark: return darkTheme;
    default: return lightTheme;
    }
}

QColor InsightVisualStyle::roleColor(InsightVisualRole role)
{
    return roleColor(role, ApplicationThemeManager::instance().mode());
}

QColor InsightVisualStyle::roleColor(InsightVisualRole role,
                                     ThemeMode mode)
{
    const InsightSemanticTokens& semantic = theme(mode).semantic;
    switch (role) {
    case InsightVisualRole::Write:
    case InsightVisualRole::Output:
        return semantic.write;
    case InsightVisualRole::Read:
    case InsightVisualRole::Input:
        return semantic.read;
    case InsightVisualRole::Port:
        return semantic.port;
    case InsightVisualRole::Condition:
    case InsightVisualRole::Control:
        return semantic.condition;
    case InsightVisualRole::Case:
        return semantic.caseRole;
    case InsightVisualRole::Timing:
        return semantic.timing;
    case InsightVisualRole::Kernel:
        return semantic.kernel;
    case InsightVisualRole::Data:
        return semantic.data;
    case InsightVisualRole::Unknown:
        return semantic.unknown;
    }
    return semantic.unknown;
}

QColor InsightVisualStyle::roleColor(const QString& roleName)
{
    const QString role = roleName.trimmed().toLower();
    if (role == QStringLiteral("write") || role == QStringLiteral("output"))
        return roleColor(InsightVisualRole::Write);
    if (role == QStringLiteral("read") || role == QStringLiteral("input"))
        return roleColor(InsightVisualRole::Read);
    if (role == QStringLiteral("port"))
        return roleColor(InsightVisualRole::Port);
    if (role == QStringLiteral("condition") || role == QStringLiteral("control"))
        return roleColor(InsightVisualRole::Condition);
    if (role == QStringLiteral("case"))
        return roleColor(InsightVisualRole::Case);
    if (role == QStringLiteral("timing"))
        return roleColor(InsightVisualRole::Timing);
    if (role == QStringLiteral("kernel"))
        return roleColor(InsightVisualRole::Kernel);
    if (role == QStringLiteral("data"))
        return roleColor(InsightVisualRole::Data);
    return roleColor(InsightVisualRole::Unknown);
}

QColor InsightVisualStyle::roleFillColor(InsightVisualRole role)
{
    return roleFillColor(role, ApplicationThemeManager::instance().mode());
}

QColor InsightVisualStyle::roleFillColor(InsightVisualRole role,
                                         ThemeMode mode)
{
    const InsightSemanticTokens& semantic = theme(mode).semantic;
    switch (role) {
    case InsightVisualRole::Write:
    case InsightVisualRole::Output:
        return semantic.writeFill;
    case InsightVisualRole::Read:
    case InsightVisualRole::Input:
        return semantic.readFill;
    case InsightVisualRole::Port:
        return semantic.portFill;
    case InsightVisualRole::Kernel:
        return semantic.kernelFill;
    case InsightVisualRole::Condition:
    case InsightVisualRole::Control:
        return semantic.conditionFill;
    case InsightVisualRole::Case:
        return semantic.caseFill;
    case InsightVisualRole::Timing:
        return semantic.timingFill;
    case InsightVisualRole::Data:
        return semantic.dataFill;
    case InsightVisualRole::Unknown:
        return semantic.unknownFill;
    }
    return semantic.unknownFill;
}

QColor InsightVisualStyle::heatIntensityColor(double intensity)
{
    return heatIntensityColor(
        intensity, ApplicationThemeManager::instance().mode());
}

QColor InsightVisualStyle::heatIntensityColor(double intensity,
                                              ThemeMode mode)
{
    const double t = std::clamp(intensity, 0.0, 1.0);
    const InsightSemanticTokens& semantic = theme(mode).semantic;
    if (t < 0.5)
        return mix(semantic.heatLow, semantic.heatMid, t * 2.0);
    return mix(semantic.heatMid,
               semantic.heatHigh,
               (t - 0.5) * 2.0);
}

QPen InsightVisualStyle::hairlinePen(const QColor& color)
{
    QPen pen(color, 1.0);
    pen.setCosmetic(true);
    return pen;
}

QPen InsightVisualStyle::panelBorderPen()
{
    return hairlinePen(theme().border);
}

QPen InsightVisualStyle::selectedPen(qreal width)
{
    return QPen(theme().selected, width);
}

QPen InsightVisualStyle::hoverPen(qreal width)
{
    return QPen(theme().hover, width);
}

QBrush InsightVisualStyle::panelBrush()
{
    return QBrush(theme().panelBackground);
}

QBrush InsightVisualStyle::canvasBrush()
{
    return QBrush(theme().canvasBackground);
}

QFont InsightVisualStyle::titleFont(const QFont& base)
{
    QFont font = base;
    font.setBold(true);
    font.setPointSize(qMax(9, font.pointSize()));
    return font;
}

QFont InsightVisualStyle::compactFont(const QFont& base)
{
    QFont font = base;
    font.setPointSize(qMax(8, font.pointSize() - 1));
    return font;
}

QFont InsightVisualStyle::labelFont(const QFont& base)
{
    QFont font = compactFont(base);
    font.setBold(true);
    return font;
}

QPalette InsightVisualStyle::applicationPalette()
{
    return applicationPalette(
        ApplicationThemeManager::instance().mode());
}

QPalette InsightVisualStyle::applicationPalette(ThemeMode mode)
{
    const InsightTheme& t = theme(mode);
    QPalette palette;
    palette.setColor(QPalette::Window, t.appBackground);
    palette.setColor(QPalette::WindowText, t.textPrimary);
    palette.setColor(QPalette::Base, t.input.background);
    palette.setColor(QPalette::AlternateBase,
                     t.itemView.alternateBackground);
    palette.setColor(QPalette::ToolTipBase, t.panelBackground);
    palette.setColor(QPalette::ToolTipText, t.textPrimary);
    palette.setColor(QPalette::Text, t.input.text);
    palette.setColor(QPalette::Button, t.button.background);
    palette.setColor(QPalette::ButtonText, t.button.text);
    palette.setColor(QPalette::BrightText,
                     t.statusBar.errorText);
    palette.setColor(QPalette::Highlight,
                     t.input.selectionBackground);
    palette.setColor(QPalette::HighlightedText,
                     t.input.selectionText);
    palette.setColor(QPalette::Link, t.accent);
    palette.setColor(QPalette::LinkVisited, t.selected);
    palette.setColor(QPalette::PlaceholderText, t.textMuted);
    palette.setColor(QPalette::Light, t.panelSubtle);
    palette.setColor(QPalette::Midlight, t.border);
    palette.setColor(QPalette::Mid, t.borderStrong);
    palette.setColor(QPalette::Dark, t.borderStrong);
    palette.setColor(QPalette::Shadow, t.canvasBackground);

    for (QPalette::ColorRole role :
         {QPalette::WindowText,
          QPalette::Text,
          QPalette::ButtonText,
          QPalette::PlaceholderText}) {
        palette.setColor(QPalette::Disabled,
                         role,
                         t.button.textDisabled);
    }
    palette.setColor(QPalette::Disabled,
                     QPalette::Button,
                     t.panelSubtle);
    palette.setColor(QPalette::Disabled,
                     QPalette::Base,
                     t.panelSubtle);
    palette.setColor(QPalette::Disabled,
                     QPalette::Highlight,
                     t.borderStrong);
    palette.setColor(QPalette::Disabled,
                     QPalette::HighlightedText,
                     t.button.textDisabled);
    return palette;
}

QColor InsightVisualStyle::subtleBorder(const QColor& surface, const QColor& foreground)
{
    return mix(surface, foreground, surface.lightnessF() > 0.5 ? 0.10 : 0.12);
}

QString InsightVisualStyle::panelStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  color: %3;"
               "}")
        .arg(objectSelector(QStringLiteral("QWidget"), objectName),
             t.panelBackground.name(),
             t.textPrimary.name())
        + floatingBackgroundRule(QStringLiteral("QWidget"), objectName);
}

QString InsightVisualStyle::applicationStyleSheet()
{
    return applicationStyleSheet(
        ApplicationThemeManager::instance().mode());
}

QString InsightVisualStyle::applicationStyleSheet(ThemeMode mode)
{
    InsightTheme t = theme(mode);
    // Soften shell decoration locally; retain the shared graph and focus tokens.
    t.border = subtleBorder(t.panelBackground, t.textPrimary);
    t.menu.border = subtleBorder(t.menu.background, t.textPrimary);
    t.dock.border = subtleBorder(t.dock.background, t.textPrimary);
    t.dock.titleBorder = subtleBorder(t.dock.titleBackground, t.textPrimary);
    t.itemView.headerBorder = subtleBorder(t.itemView.headerBackground, t.textPrimary);
    t.splitterHandle = t.border;
    QString result = QStringLiteral(
               "QMainWindow { background: %1; }"
               "QMenuBar { background: %2; border-bottom: 1px solid %3; "
               "padding: 3px 10px; spacing: 18px; color: %4; }"
               "QMenuBar::item { padding: 5px 10px; border-radius: 4px; }"
               "QMenuBar::item:selected { background: %5; color: %6; }"
               "QMenu { background: %2; border: 1px solid %3; "
               "padding: 5px; color: %4; }"
               "QMenu::item { padding: 5px 24px 5px 18px; "
               "border-radius: 4px; }"
               "QMenu::item:selected { background: %5; color: %6; }"
               "QStatusBar { background: %7; border-top: 1px solid %8; "
               "color: %9; min-height: 22px; }"
               "QStatusBar QLabel { color: %10; }"
               "QTabWidget::pane { border: 0; background: %11; }"
               "QTabBar { background: %12; }"
               "QTabBar::tab { background: %13; color: %14; "
               "border: 0; border-top-color: %15; border-bottom: 2px solid transparent; border-top-left-radius: 8px; border-top-right-radius: 8px; "
               "padding: 6px 13px; margin-right: 2px; min-height: 20px; }"
               "QTabBar::tab:selected { background: %16; color: %17; "
               "border-bottom-color: %18; }"
               "QTabBar::tab:hover { background: %19; color: %20; }"
               "QDockWidget { background: %21; color: %22; "
               "border: 1px solid %23; titlebar-close-icon: url(none); }"
               "QDockWidget::title { background: %24; padding: 5px 8px; "
               "border-bottom: 1px solid %25; font-weight: 600; }"
               "QLineEdit { background: %26; color: %27; "
               "border: 1px solid %28; border-radius: 8px; padding: 6px 10px; "
               "selection-background-color: %29; }"
               "QLineEdit:focus { border-color: %30; }"
               "QTextEdit, QPlainTextEdit { background: %26; color: %27; "
               "border: 1px solid %28; border-radius: 8px; padding: 6px 10px; "
               "selection-background-color: %29; }"
               "QTextEdit:focus, QPlainTextEdit:focus { border-color: %30; }"
               "QPlainTextEdit[codeEditorSurface=\"true\"] { border: 0; "
               "border-radius: 0; padding: 0; }"
               "QPlainTextEdit[codeEditorSurface=\"true\"]:focus { "
               "border: 0; }"
               "QComboBox, QAbstractSpinBox { background: %26; color: %27; "
               "border: 1px solid %28; border-radius: 8px; padding: 6px 10px; "
               "selection-background-color: %29; }"
               "QComboBox:focus, QAbstractSpinBox:focus { border-color: %30; }"
               "QComboBox::drop-down { border: 0; width: 20px; }"
               "QTreeWidget, QTreeView, QListWidget, QListView { "
               "background: %31; alternate-background-color: %32; "
               "border: 1px solid %33; color: %34; }"
               "QTreeWidget::item:hover, QTreeView::item:hover, "
               "QListWidget::item:hover, QListView::item:hover { "
               "background: %35; }"
               "QTreeWidget::item:selected, QTreeView::item:selected, "
               "QListWidget::item:selected, QListView::item:selected { "
               "background: %36; color: %34; }"
               "QHeaderView::section { background: %37; border: 0; "
               "border-bottom: 1px solid %38; padding: 4px 6px; "
               "color: %39; }"
               "QTableWidget, QTableView { background: %31; "
               "alternate-background-color: %32; border: 1px solid %33; "
               "gridline-color: %38; color: %34; }"
               "QTableWidget::item:hover, QTableView::item:hover { "
               "background: %35; }"
               "QTableWidget::item:selected, QTableView::item:selected { "
               "background: %36; color: %34; }"
               "QToolBar { background: %41; border: 0; spacing: 4px; "
               "padding: 3px; }"
               "QToolBar::separator { background: %40; width: 1px; "
               "margin: 4px; }"
               "QSplitter::handle { background: %40; }"
               "QSplitter::handle:hover { background: %42; }"
               "QScrollBar:vertical, QScrollBar:horizontal { "
               "background: %43; border: 0; margin: 0; }"
               "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { "
               "background: %40; border-radius: 4px; min-height: 24px; "
               "min-width: 24px; }"
               "QScrollBar::handle:vertical:hover, "
               "QScrollBar::handle:horizontal:hover { background: %42; }"
               "QScrollBar::add-line, QScrollBar::sub-line, "
               "QScrollBar::add-page, QScrollBar::sub-page { "
               "background: transparent; border: 0; width: 0; height: 0; }")
        .arg(t.appBackground.name(),
             t.menu.background.name(),
             t.menu.border.name(),
             t.menu.text.name(),
             t.menu.itemHoverBackground.name(),
             t.menu.itemHoverText.name(),
             t.statusBar.background.name(),
             t.statusBar.border.name(),
             t.statusBar.text.name(),
             t.textSecondary.name(),
             t.panelBackground.name(),
             t.tab.barBackground.name(),
             t.tab.tabBackground.name(),
             t.tab.text.name(),
             t.tab.border.name(),
             t.tab.tabBackgroundSelected.name(),
             t.tab.textSelected.name(),
             t.tab.borderSelected.name(),
             t.tab.tabBackgroundHover.name(),
             t.tab.textHover.name(),
             t.dock.background.name(),
             t.dock.text.name(),
             t.dock.border.name(),
             t.dock.titleBackground.name(),
             t.dock.titleBorder.name(),
             t.input.background.name(),
             t.input.text.name(),
             t.input.border.name(),
             t.input.selectionBackground.name(),
             t.input.focusBorder.name(),
             t.itemView.background.name(),
             t.itemView.alternateBackground.name(),
             t.itemView.border.name(),
             t.itemView.text.name(),
             t.itemView.hoverBackground.name(),
             t.itemView.selectedBackground.name(),
             t.itemView.headerBackground.name(),
             t.itemView.headerBorder.name(),
             t.itemView.headerText.name(),
             t.splitterHandle.name(),
             t.toolbarBackground.name(),
             t.borderStrong.name(),
             t.panelSubtle.name());

    result += QStringLiteral(
                  "QWidget { color: %1; }"
                  "QMainWindow, QDialog, QMessageBox { "
                  "background: %2; color: %1; }"
                  "QToolTip { background: %3; color: %1; "
                  "border: 1px solid %4; padding: 4px 6px; }"
                  "QLabel:disabled { color: %5; }"
                  "QMenu::item:disabled { color: %5; background: transparent; }"
                  "QMenu::separator { background: %4; height: 1px; "
                  "margin: 4px 8px; }"
                  "QLineEdit:disabled, QTextEdit:disabled, "
                  "QPlainTextEdit:disabled, QComboBox:disabled, "
                  "QAbstractSpinBox:disabled { background: %6; color: %5; "
                  "border-color: %4; }"
                  "QComboBox QAbstractItemView { background: %3; color: %1; "
                  "border: 1px solid %4; selection-background-color: %7; "
                  "selection-color: %8; outline: 0; }"
                  "QAbstractItemView:disabled { background: %6; color: %5; }"
                  "QTreeView::item:selected:!active, QListView::item:selected:!active, "
                  "QTableView::item:selected:!active { background: %9; color: %1; }"
                  "QRadioButton { color: %1; spacing: 6px; "
                  "padding: 2px; }"
                  "QRadioButton:hover { background: %10; "
                  "border-radius: 4px; }"
                  "QRadioButton:disabled { color: %5; }"
                  "QGroupBox { color: %1; border: 1px solid %4; "
                  "border-radius: 6px; margin-top: 10px; padding-top: 8px; }"
                  "QGroupBox::title { subcontrol-origin: margin; left: 8px; "
                  "padding: 0 4px; color: %11; }"
                  "QAbstractScrollArea, QScrollArea, QStackedWidget { "
                  "background: %3; color: %1; border-color: %4; }"
                  "QScrollArea > QWidget > QWidget { background: %3; }"
                  "QGraphicsView { background: %12; color: %1; }"
                  "QTableCornerButton::section { background: %6; "
                  "border: 1px solid %4; }"
                  "QProgressBar { background: %6; color: %1; "
                  "border: 1px solid %4; border-radius: 4px; text-align: center; }"
                  "QProgressBar::chunk { background: %13; border-radius: 3px; }"
                  "QStatusBar::item { border: 0; }"
                  "QToolBar:disabled { color: %5; }")
                  .arg(t.textPrimary.name(),
                       t.appBackground.name(),
                       t.panelBackground.name(),
                       t.border.name(),
                       t.button.textDisabled.name(),
                       t.panelSubtle.name(),
                       t.input.selectionBackground.name(),
                       t.input.selectionText.name(),
                       t.itemView.selectedBackground.name(),
                       t.itemView.hoverBackground.name(),
                       t.textSecondary.name(),
                       t.canvasBackground.name(),
                       t.accent.name());
    result += InsightControlStyle::styleSheet(t);
    result += QStringLiteral(
                  "QToolBar#contextRail { background: %1; "
                  "border-left: 1px solid %2; padding: 4px; spacing: 3px; }"
                  "QToolBar#contextRail QToolButton:checked { "
                  "background: %3; border-color: %4; color: %5; }"
                  "QWidget#contextPeekHost { background: %6; color: %7; "
                  "border-left: 1px solid %8; }"
                  "QWidget#contextPeekHeader { background: %1; "
                  "border-bottom: 1px solid %2; }"
                  "QLabel#contextPeekTitle { color: %7; font-weight: 600; }"
                  "QWidget#contextPeekContent, "
                  "QWidget#temporaryEditorContextView, "
                  "QWidget#temporaryEditorContextContent { background: %6; }"
                  "QWidget#temporaryEditorContextSearchBar { "
                  "background: %9; border-bottom: 1px solid %2; }"
                  "QWidget#temporaryEditorContextSearchBar QToolButton { "
                  "padding: 3px; min-width: 24px; min-height: 24px; }"
                  "QDockWidget#contextWorkspaceDock { background: %6; "
                  "border-left: 1px solid %8; }"
                  "QTabWidget#contextDockTabs::pane { background: %6; "
                  "border: 0; border-top: 1px solid %2; }"
                  "QTabWidget#contextDockTabs QTabBar::tab { "
                  "padding: 5px 10px; min-height: 19px; }")
                  .arg(t.toolbarBackground.name(),
                       t.border.name(),
                       t.button.backgroundChecked.name(),
                       t.button.borderChecked.name(),
                       t.button.textChecked.name(),
                       t.panelBackground.name(),
                       t.textPrimary.name(),
                       t.border.name(),
                       t.panelSubtle.name());
    result += QStringLiteral(
                  "QTabBar::tab:focus { "
                  "border: %1px solid %2; }"
                  "QTreeView:focus, QListView:focus, QTableView:focus { "
                  "border: %1px solid %2; outline: 0; }")
                  .arg(t.focus.width)
                  .arg(t.focus.ring.name());
    result += QStringLiteral(
        "QToolBar#contextRail { padding: 6px; spacing: 6px; }"
        "QFrame#projectSidebarHeader { background: transparent; border: 0; }"
        "QFrame#projectSidebarHeader QToolButton, QToolBar#contextRail QToolButton {"
        " min-width: 36px; min-height: 36px;"
        " padding: 0; border: 1px solid transparent; border-radius: 8px; background: transparent; }"
        "QToolButton#projectRailButton::menu-indicator { image: none; }"
        "QFrame#projectSidebarHeader QToolButton:hover { background: %4; }"
        "QToolBar#contextRail QToolButton:hover { background: %1; }"
        "QFrame#projectSidebarHeader QToolButton:checked, QToolBar#contextRail QToolButton:checked {"
        " background: %2; color: %3; border-color: transparent; }"
        "QFrame#projectSidebarHeader QToolButton:hover:checked { background: %4; }"
        "QToolBar#contextRail QToolButton:hover:checked { background: %1; }"
        "QFrame#projectSidebarHeader QToolButton:focus, QToolBar#contextRail QToolButton:focus { border-color: %3; }"
        "QFrame#workspaceTitleBar QToolButton { min-width: 28px; min-height: 24px; padding: 2px 6px;"
        " background: transparent; border: 0; border-radius: 8px; }"
        "QFrame#workspaceTitleBar QToolButton:hover { background: %1; }"
        "QTreeView, QListView, QTableView, QDockWidget { border: 0; }"
        "QTreeView::item, QListView::item { padding: 3px 4px; min-height: 22px; border-radius: 6px; }"
        "QTreeView:focus, QListView:focus, QTableView:focus { border: 0; outline: 0; }"
        "QHeaderView::section { padding: 7px 8px; }"
        "QScrollBar:vertical { width: 10px; } QScrollBar:horizontal { height: 10px; }"
        "QScrollBar::handle:vertical { min-width: 0; margin: 2px; border-radius: 3px; }"
        "QScrollBar::handle:horizontal { min-height: 0; margin: 2px; border-radius: 3px; }")
        .arg(t.hover.name(), t.itemView.selectedBackground.name(), t.accent.name(),
             mix(t.dock.background, t.textPrimary, 0.12).name());
    const QColor navigationBase = mix(t.panelBackground, t.textPrimary, 0.025);
    const QColor navigationHover = mix(navigationBase, t.textPrimary, 0.09);
    const QColor navigationSelected = mix(navigationBase, t.accent, 0.20);
    result += QStringLiteral(
        "QTreeView[workspaceNavigationList=true], QListView[workspaceNavigationList=true] {"
        " background: %1; alternate-background-color: %1; }"
        "QTreeView[workspaceNavigationList=true]::item, QListView[workspaceNavigationList=true]::item {"
        " background: %1; }"
        "QTreeView[workspaceNavigationList=true]::item:hover:!selected, QListView[workspaceNavigationList=true]::item:hover:!selected {"
        " background: %2; }"
        "QTreeView[workspaceNavigationList=true]::item:selected, QListView[workspaceNavigationList=true]::item:selected,"
        "QTreeView[workspaceNavigationList=true]::item:selected:!active, QListView[workspaceNavigationList=true]::item:selected:!active {"
        " background: %3; color: %4; }"
        "QTreeView[workspaceNavigationList=true]::item:selected:hover, QListView[workspaceNavigationList=true]::item:selected:hover {"
        " background: %5; color: %4; }")
        .arg(navigationBase.name(), navigationHover.name(), navigationSelected.name(),
             t.textPrimary.name(), mix(navigationBase, t.accent, 0.28).name());
    result += QStringLiteral(
        "QDockWidget::title { font-size: 14px; font-weight: 600; padding: 9px 10px; }"
        "QTabBar::tab { padding: 8px 14px; font-weight: 400; }"
        "QTabBar::tab:selected { font-weight: 600; }"
        "QHeaderView::section { font-size: 12px; font-weight: 600; color: %1; padding: 8px; }"
        "QMenu::item { padding: 7px 28px 7px 14px; }"
        "QTreeView::item, QListView::item { min-height: 24px; padding: 3px 6px; }"
        "QGroupBox { margin-top: 14px; padding-top: 12px; }"
        "QGroupBox::title { font-weight: 600; padding: 0 6px; }"
        "QLabel[uiTextRole=metadata] { color: %1; padding-top: 2px; padding-bottom: 2px; }"
        "QLabel[uiTextRole=section] { color: %1; padding-top: 4px; padding-bottom: 4px; }"
        "QLabel[uiTextRole=pageTitle] { padding-top: 4px; padding-bottom: 8px; }"
        "QLabel[uiTextRole=panelTitle] { padding-top: 4px; padding-bottom: 4px; }"
        "QPlainTextEdit#activityOutputText { padding: 10px 12px; border: 0; }"
        "QTabWidget#contextDockTabs QTabBar::tab { padding: 8px 10px; }"
        "QLabel#contextPeekTitle { font-size: 14px; padding: 4px 0; }")
        .arg(t.textSecondary.name());
    return result;
}

QString InsightVisualStyle::chromeStyleSheet(ThemeMode mode)
{
    const auto& t = theme(mode);
    const auto border = subtleBorder(t.panelBackground, t.textPrimary).name();
    // This is an explicit ownership list. No runtime QSS parsing or broad
    // QWidget color rule: either would override QStyle's state-dependent ink.
    return QStringLiteral(
        "QMainWindow, QDialog, QMessageBox { background: %1; color: %2; }"
        "QToolTip { background: %3; color: %2; border: 1px solid %4; padding: 4px 6px; }"
        "QLabel:disabled { color: %5; }"
        "QMenu { background: %3; color: %2; border: 1px solid %4; padding: 5px; }"
        "QMenu::item { padding: 7px 28px 7px 14px; border-radius: 4px; }"
        "QMenu::item:selected { background: %6; }"
        "QMenu::item:disabled { color: %5; }"
        "QMenu::separator { background: %4; height: 1px; margin: 4px 8px; }"
        "QDockWidget { background: %3; color: %2; border: 0; titlebar-close-icon: url(none); }"
        "QDockWidget::title { background: %7; padding: 9px 10px; font-size: 14px; font-weight: 600; }"
        "QToolBar { background: %7; border: 0; spacing: 4px; padding: 3px; }"
        "QToolBar::separator { background: %4; width: 1px; margin: 4px; }"
        "QSplitter::handle { background: %4; }"
        "QSplitter::handle:hover { background: %8; }"
        "QScrollArea, QStackedWidget { background: %3; }"
        "QScrollArea > QWidget > QWidget { background: %3; }"
        "QGraphicsView { background: %9; color: %2; }"
        "QWidget#contextPeekHost { background: %3; color: %2; border-left: 1px solid %4; }"
        "QWidget#contextPeekHeader { background: %7; border-bottom: 1px solid %4; }"
        "QWidget#contextPeekContent, QWidget#temporaryEditorContextView,"
        " QWidget#temporaryEditorContextContent { background: %3; }"
        "QWidget#temporaryEditorContextSearchBar { background: %7; border-bottom: 1px solid %4; }"
        "QToolBar#contextRail { background: %7; border-left: 1px solid %4; padding: 6px; spacing: 6px; }"
        "QFrame#projectSidebarHeader { background: transparent; border: 0; }"
        "QFrame#projectSidebarHeader QToolButton, QToolBar#contextRail QToolButton {"
        " min-width: 36px; min-height: 36px; padding: 0; border: 1px solid transparent;"
        " border-radius: 8px; background: transparent; }"
        "QToolButton#projectRailButton::menu-indicator { image: none; }"
        "QFrame#projectSidebarHeader QToolButton:hover { background: %10; }"
        "QToolBar#contextRail QToolButton:hover { background: %11; }"
        "QFrame#projectSidebarHeader QToolButton:checked, QToolBar#contextRail QToolButton:checked {"
        " background: %6; color: %12; }"
        "QFrame#projectSidebarHeader QToolButton:focus, QToolBar#contextRail QToolButton:focus { border-color: %12; }"
        "QFrame#workspaceTitleBar QToolButton { min-width: 28px; min-height: 24px; padding: 2px 6px;"
        " background: transparent; border: 0; border-radius: 8px; }"
        "QFrame#workspaceTitleBar QToolButton:hover { background: %11; }"
        "QLabel[uiTextRole=metadata] { color: %13; padding-top: 2px; padding-bottom: 2px; }"
        "QLabel[uiTextRole=section] { color: %13; padding-top: 4px; padding-bottom: 4px; }"
        "QLabel[uiTextRole=pageTitle] { padding-top: 4px; padding-bottom: 8px; }"
        "QLabel[uiTextRole=panelTitle] { padding-top: 4px; padding-bottom: 4px; }"
        "QLabel#contextPeekTitle { color: %2; font-size: 14px; font-weight: 600; padding: 4px 0; }")
        .arg(t.appBackground.name(), t.textPrimary.name(), t.panelBackground.name(), border,
             t.button.textDisabled.name(), t.itemView.selectedBackground.name(), t.toolbarBackground.name(),
             t.borderStrong.name(), t.canvasBackground.name(), mix(t.dock.background, t.textPrimary, 0.12).name(),
             t.hover.name(), t.accent.name(), t.textSecondary.name());
}

QString InsightVisualStyle::tabBarStyleSheet(const QString& objectName)
{
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Qlementine)
        return {};
    const InsightTheme t = theme();
    const QString selector =
        objectSelector(QStringLiteral("QTabBar"), objectName);
    return QStringLiteral(
               "%1 { background: %2; border-bottom: 1px solid %3; }"
               "%1::tab {"
               "  background: %4;"
               "  color: %5;"
               "  padding: 8px 14px; font-weight: 400;"
               "  border: 0; border-top-color: %6; border-bottom: 2px solid transparent;"
               "  border-top-left-radius: 8px; border-top-right-radius: 8px;"
               "  margin-right: 2px;"
               "  min-height: 20px;"
               "  min-width: 108px;"
               "}"
               "%1::tab:selected {"
               "  background: %7;"
               "  color: %8;"
               "  border-bottom-color: %9; font-weight: 600;"
               "}"
               "%1::tab:hover {"
               "  background: %10;"
               "  color: %11;"
               "}")
        .arg(selector,
             t.tab.barBackground.name(),
             subtleBorder(t.tab.barBackground, t.textPrimary).name(),
             t.tab.tabBackground.name(),
             t.tab.text.name(),
             t.tab.border.name(),
             t.tab.tabBackgroundSelected.name(),
             t.tab.textSelected.name(),
             t.tab.borderSelected.name(),
             t.tab.tabBackgroundHover.name(),
             t.tab.textHover.name());
}

QString InsightVisualStyle::labelStyleSheet(const QString& objectName,
                                            bool strong)
{
    const InsightTheme t = theme();
    return QStringLiteral("%1 { color: %2; %3 }")
        .arg(objectSelector(QStringLiteral("QLabel"), objectName),
             strong ? t.textPrimary.name() : t.textSecondary.name(),
             strong ? QStringLiteral("font-weight: 600;") : QString());
}

QString InsightVisualStyle::statusChipStyleSheet(InsightStatusTone tone,
                                                 const QString& objectName)
{
    const InsightTheme t = theme();
    QColor text = t.statusBar.infoText;
    QColor background = t.statusBar.infoBackground;
    QColor border = t.statusBar.infoBorder;
    switch (tone) {
    case InsightStatusTone::Success:
        text = t.statusBar.successText;
        background = t.statusBar.successBackground;
        border = t.statusBar.successBorder;
        break;
    case InsightStatusTone::Warning:
        text = t.statusBar.warningText;
        background = t.statusBar.warningBackground;
        border = t.statusBar.warningBorder;
        break;
    case InsightStatusTone::Error:
        text = t.statusBar.errorText;
        background = t.statusBar.errorBackground;
        border = t.statusBar.errorBorder;
        break;
    case InsightStatusTone::Info:
        break;
    }
    background.setAlpha(68);
    border.setAlpha(180);
    return QStringLiteral(
               "%1 {"
               "  color: %2;"
               "  background: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 8px;"
               "  padding: 2px 8px;"
               "}")
        .arg(objectSelector(QStringLiteral("QLabel"), objectName),
             text.name(),
             background.name(QColor::HexArgb),
             border.name(QColor::HexArgb));
}

QString InsightVisualStyle::dockAttentionStyleSheet(
    const QString& objectName,
    InsightStatusTone tone)
{
    const InsightTheme t = theme();
    QColor background = t.statusBar.warningBackground;
    QColor border = t.statusBar.warningBorder;
    if (tone == InsightStatusTone::Success) {
        background = t.statusBar.successBackground;
        border = t.statusBar.successBorder;
    } else if (tone == InsightStatusTone::Error) {
        background = t.statusBar.errorBackground;
        border = t.statusBar.errorBorder;
    } else if (tone == InsightStatusTone::Info) {
        background = t.statusBar.infoBackground;
        border = t.statusBar.infoBorder;
    }
    background.setAlpha(96);
    border.setAlpha(180);
    return QStringLiteral(
               "QDockWidget#%1::title {"
               "  background: %2;"
               "  padding-left: 6px;"
               "}"
               "QDockWidget#%1 {"
               "  border: 1px solid %3;"
               "}")
        .arg(objectName,
             background.name(QColor::HexArgb),
             border.name(QColor::HexArgb));
}

QString InsightVisualStyle::globalControlPanelStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    const QString selector =
        objectSelector(QStringLiteral("QFrame"), objectName);
    if (ApplicationThemeManager::instance().backend() != UiStyleBackend::Classic) {
        return QStringLiteral(
            "%1 { background: %2; border: 1px solid %3; border-radius: 8px; }"
            "%1 QLabel { color: %4; font-weight: 600; padding: 10px 12px 2px; }")
            .arg(selector, t.panelBackground.name(), t.borderStrong.name(), t.textSecondary.name());
    }
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  color: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 8px;"
               "}"
               "%1 QLabel {"
               "  color: %5;"
               "  font-weight: 600;"
               "  padding: 10px 12px 2px 12px;"
               "}"
               "%1 QLineEdit {"
               "  margin: 6px 10px;"
               "  padding: 8px;"
               "  border: 1px solid %6;"
               "  border-radius: 8px;"
               "  background: %7;"
               "  color: %3;"
               "  selection-background-color: %8;"
               "}"
               "%1 QLineEdit:focus { border-color: %9; }"
               "%1 QListWidget {"
               "  margin: 4px 10px 10px 10px;"
               "  border: 0;"
               "  background: %2;"
               "  color: %10;"
               "  outline: 0;"
               "}"
               "%1 QListWidget::item {"
               "  padding: 6px 8px;"
               "  border-radius: 8px;"
               "}"
               "%1 QListWidget::item:hover { background: %11; }"
               "%1 QListWidget::item:selected {"
               "  background: %12;"
               "  color: %3;"
               "}")
        .arg(selector,
             t.panelBackground.name(),
             t.textPrimary.name(),
             t.borderStrong.name(),
             t.textSecondary.name(),
             t.input.border.name(),
             t.panelSubtle.name(),
             t.input.selectionBackground.name(),
             t.input.focusBorder.name(),
             t.itemView.text.name(),
             t.itemView.hoverBackground.name(),
             t.itemView.selectedBackground.name());
}

QString InsightVisualStyle::graphViewStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  border: 1px solid %3;"
               "  border-radius: 8px;"
               "}")
        .arg(objectSelector(QStringLiteral("QGraphicsView"), objectName),
             t.graph.background.name(),
             t.border.name())
        + floatingBackgroundRule(QStringLiteral("QGraphicsView"), objectName);
}

QString InsightVisualStyle::titleBarStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  color: %2;"
               "  background: %3;"
               "  border: 0; border-bottom: 1px solid %4;"
               "  border-radius: 0;"
               "  padding: 5px 8px;"
               "  font-weight: 600;"
               "}")
        .arg(objectSelector(QStringLiteral("QLabel"), objectName),
             t.textPrimary.name(),
             t.panelSubtle.name(),
             subtleBorder(t.panelSubtle, t.textPrimary).name())
        + floatingBackgroundRule(QStringLiteral("QLabel"), objectName);
}

QString InsightVisualStyle::compactSearchFieldStyleSheet(
    const QString& objectName)
{
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Qlementine)
        return floatingBackgroundRule(QStringLiteral("QLineEdit"), objectName);
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  color: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 8px;"
               "  padding: 4px 8px;"
               "  min-height: 22px;"
               "}"
               "%1:focus {"
               "  border-color: %5;"
               "}")
        .arg(objectSelector(QStringLiteral("QLineEdit"), objectName),
             t.panelBackground.name(),
             t.textPrimary.name(),
             t.border.name(),
             t.accent.name())
        + floatingBackgroundRule(QStringLiteral("QLineEdit"), objectName);
}

QString InsightVisualStyle::sideInspectorStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: transparent;"
               "  border: 0;"
               "  border-left: 1px solid %2;"
               "  border-radius: 0;"
               "}")
        .arg(objectSelector(QStringLiteral("QWidget"), objectName),
             subtleBorder(t.panelBackground, t.textPrimary).name());
}

void InsightVisualStyle::applyPanel(QWidget* widget)
{
    if (!widget)
        return;
    registerThemedWidget(widget, QStringLiteral("panel"));
}

void InsightVisualStyle::applyTitleLabel(QLabel* label)
{
    if (!label)
        return;
    UiTypography::apply(label, UiTypography::Role::PanelTitle);
    registerThemedWidget(label, QStringLiteral("title"));
    label->setMinimumHeight(minimumControlHeight(label));
}

void InsightVisualStyle::applyLabel(QLabel* label, bool strong)
{
    if (!label)
        return;
    UiTypography::apply(label, strong ? UiTypography::Role::Section : UiTypography::Role::Metadata);
    registerThemedWidget(
        label,
        strong ? QStringLiteral("strongLabel")
               : QStringLiteral("label"));
}

void InsightVisualStyle::applySearchField(QLineEdit* edit)
{
    if (!edit)
        return;
    edit->setFont(UiTypography::font());
    registerThemedWidget(edit, QStringLiteral("search"));
    edit->setMinimumHeight(minimumControlHeight(edit));
    // A readable default in character units; CompactFlowLayout releases this
    // minimum when the field shares a wrapping toolbar with action controls.
    edit->setMinimumWidth(qMax(edit->minimumSizeHint().width(),
                              edit->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 18));
}

void InsightVisualStyle::applyToolbarButton(QPushButton* button)
{
    applyControlRole(button, QStringLiteral("toolbarButton"));
}

void InsightVisualStyle::applyPrimaryButton(QPushButton* button)
{
    applyControlRole(button, QStringLiteral("primaryButton"));
}

void InsightVisualStyle::applySegmentedCheckBox(QWidget* checkBox)
{
    applyControlRole(checkBox, QStringLiteral("segmentedCheckBox"));
}

void InsightVisualStyle::applyGlobalControlPanel(QWidget* widget)
{
    registerThemedWidget(widget, QStringLiteral("globalControlPanel"));
}

void InsightVisualStyle::applySideInspector(QWidget* widget)
{
    registerThemedWidget(widget, QStringLiteral("sideInspector"));
}
