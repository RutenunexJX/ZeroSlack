#include "insightvisualstyle.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
}

namespace {
InsightTheme buildTheme()
{
    InsightTheme theme;
    theme.appBackground = color("#eef1f5");
    theme.canvasBackground = color("#f6f8fb");
    theme.panelBackground = color("#ffffff");
    theme.panelSubtle = color("#f8fafc");
    theme.border = color("#d9e0ea");
    theme.borderStrong = color("#aeb8c8");
    theme.textPrimary = color("#172033");
    theme.textSecondary = color("#48566a");
    theme.textMuted = color("#7a8797");
    theme.accent = color("#2563eb");
    theme.selected = color("#db2777");
    theme.hover = color("#0f766e");
    theme.warning = color("#b45309");
    theme.splitterHandle = color("#cbd5e1");
    theme.toolbarBackground = color("#f8fafc");

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

    theme.tab.barBackground = color("#f4f7fb");
    theme.tab.tabBackground = color("#f3f6fa");
    theme.tab.tabBackgroundHover = color("#eef4ff");
    theme.tab.tabBackgroundSelected = theme.panelBackground;
    theme.tab.text = color("#475569");
    theme.tab.textHover = color("#1e40af");
    theme.tab.textSelected = color("#1d4ed8");
    theme.tab.border = color("#d8e1ec");
    theme.tab.borderSelected = color("#b9cff4");

    theme.sideRail.background = theme.panelBackground;
    theme.sideRail.border = color("#d8e1ec");
    theme.sideRail.buttonBackground = QColor(0, 0, 0, 0);
    theme.sideRail.buttonHoverBackground = color("#eef4ff");
    theme.sideRail.buttonCheckedBackground = color("#e8f1ff");
    theme.sideRail.buttonBorder = QColor(0, 0, 0, 0);
    theme.sideRail.buttonHoverBorder = color("#dbeafe");
    theme.sideRail.buttonCheckedBorder = color("#bfdbfe");
    theme.sideRail.text = color("#475569");
    theme.sideRail.textHover = color("#1d4ed8");
    theme.sideRail.textChecked = color("#1d4ed8");

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
    return theme;
}
}

const InsightTheme& InsightVisualStyle::theme()
{
    static const InsightTheme cachedTheme = buildTheme();
    return cachedTheme;
}

QColor InsightVisualStyle::roleColor(InsightVisualRole role)
{
    switch (role) {
    case InsightVisualRole::Write:
    case InsightVisualRole::Output:
        return color("#b45309");
    case InsightVisualRole::Read:
    case InsightVisualRole::Input:
        return color("#15803d");
    case InsightVisualRole::Port:
        return color("#2563eb");
    case InsightVisualRole::Condition:
    case InsightVisualRole::Control:
        return color("#7c3aed");
    case InsightVisualRole::Case:
        return color("#c026d3");
    case InsightVisualRole::Timing:
        return color("#0284c7");
    case InsightVisualRole::Kernel:
        return color("#1d4ed8");
    case InsightVisualRole::Data:
        return color("#16a34a");
    case InsightVisualRole::Unknown:
        return color("#64748b");
    }
    return color("#64748b");
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
    switch (role) {
    case InsightVisualRole::Write:
    case InsightVisualRole::Output:
        return color("#fff7ed");
    case InsightVisualRole::Read:
    case InsightVisualRole::Input:
        return color("#ecfdf3");
    case InsightVisualRole::Port:
    case InsightVisualRole::Kernel:
        return color("#eff6ff");
    case InsightVisualRole::Condition:
    case InsightVisualRole::Control:
        return color("#f5f3ff");
    case InsightVisualRole::Case:
        return color("#fdf4ff");
    case InsightVisualRole::Timing:
        return color("#e0f2fe");
    case InsightVisualRole::Data:
        return color("#dcfce7");
    case InsightVisualRole::Unknown:
        return color("#f8fafc");
    }
    return color("#f8fafc");
}

QColor InsightVisualStyle::heatIntensityColor(double intensity)
{
    const double t = std::clamp(intensity, 0.0, 1.0);
    if (t < 0.5)
        return mix(color("#f8fafc"), color("#facc15"), t * 2.0);
    return mix(color("#facc15"), color("#ef4444"), (t - 0.5) * 2.0);
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
             t.textPrimary.name());
}

QString InsightVisualStyle::applicationStyleSheet()
{
    const InsightTheme t = theme();
    return QStringLiteral(
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
               "border: 1px solid %15; border-bottom-color: %15; "
               "padding: 6px 13px; margin-right: 2px; min-height: 20px; }"
               "QTabBar::tab:selected { background: %16; color: %17; "
               "border-color: %18; border-bottom-color: %16; }"
               "QTabBar::tab:hover { background: %19; color: %20; }"
               "QDockWidget { background: %21; color: %22; "
               "border: 1px solid %23; titlebar-close-icon: url(none); }"
               "QDockWidget::title { background: %24; padding: 5px 8px; "
               "border-bottom: 1px solid %25; font-weight: 600; }"
               "QLineEdit { background: %26; color: %27; "
               "border: 1px solid %28; border-radius: 4px; padding: 5px 8px; "
               "selection-background-color: %29; }"
               "QLineEdit:focus { border-color: %30; }"
               "QTextEdit, QPlainTextEdit { background: %26; color: %27; "
               "border: 1px solid %28; border-radius: 4px; padding: 5px 8px; "
               "selection-background-color: %29; }"
               "QTextEdit:focus, QPlainTextEdit:focus { border-color: %30; }"
               "QComboBox, QAbstractSpinBox { background: %26; color: %27; "
               "border: 1px solid %28; border-radius: 4px; padding: 4px 8px; "
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
               "QPushButton, QToolButton { background: %40; color: %41; "
               "border: 1px solid %42; border-radius: 5px; "
               "padding: 5px 9px; }"
               "QPushButton:hover, QToolButton:hover { background: %43; "
               "border-color: %44; color: %45; }"
               "QPushButton:pressed, QToolButton:pressed { "
               "background: %46; }"
               "QPushButton:checked, QToolButton:checked { "
               "background: %47; border-color: %48; color: %49; }"
               "QPushButton:disabled, QToolButton:disabled { color: %50; }"
               "QToolBar { background: %52; border: 0; spacing: 4px; "
               "padding: 3px; }"
               "QToolBar::separator { background: %51; width: 1px; "
               "margin: 4px; }"
               "QSplitter::handle { background: %51; }"
               "QSplitter::handle:hover { background: %53; }"
               "QScrollBar:vertical, QScrollBar:horizontal { "
               "background: %54; border: 0; margin: 0; }"
               "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { "
               "background: %51; border-radius: 4px; min-height: 24px; "
               "min-width: 24px; }"
               "QScrollBar::handle:vertical:hover, "
               "QScrollBar::handle:horizontal:hover { background: %53; }"
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
             t.button.background.name(),
             t.button.text.name(),
             t.button.border.name(),
             t.button.backgroundHover.name(),
             t.button.borderHover.name(),
             t.button.textHover.name(),
             t.button.backgroundPressed.name(),
             t.button.backgroundChecked.name(),
             t.button.borderChecked.name(),
             t.button.textChecked.name(),
             t.button.textDisabled.name(),
             t.splitterHandle.name(),
             t.toolbarBackground.name(),
             t.borderStrong.name(),
             t.panelSubtle.name());
}

QString InsightVisualStyle::tabBarStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    const QString selector =
        objectSelector(QStringLiteral("QTabBar"), objectName);
    return QStringLiteral(
               "%1 { background: %2; border-bottom: 1px solid %3; }"
               "%1::tab {"
               "  background: %4;"
               "  color: %5;"
               "  padding: 6px 13px;"
               "  border: 1px solid %6;"
               "  border-bottom-color: %6;"
               "  margin-right: 2px;"
               "  min-height: 20px;"
               "}"
               "%1::tab:selected {"
               "  background: %7;"
               "  color: %8;"
               "  border-color: %9;"
               "  border-bottom-color: %7;"
               "}"
               "%1::tab:hover {"
               "  background: %10;"
               "  color: %11;"
               "}")
        .arg(selector,
             t.tab.barBackground.name(),
             t.tab.border.name(),
             t.tab.tabBackground.name(),
             t.tab.text.name(),
             t.tab.border.name(),
             t.tab.tabBackgroundSelected.name(),
             t.tab.textSelected.name(),
             t.tab.borderSelected.name(),
             t.tab.tabBackgroundHover.name(),
             t.tab.textHover.name());
}

QString InsightVisualStyle::workspaceTabBarStyleSheet(
    const QString& objectName)
{
    return tabBarStyleSheet(objectName);
}

QString InsightVisualStyle::sideRailStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 { background: %2; border-right: 1px solid %3; }")
        .arg(objectSelector(QStringLiteral("QWidget"), objectName),
             t.sideRail.background.name(),
             t.sideRail.border.name());
}

QString InsightVisualStyle::sideRailButtonStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 { background: %2; border: 1px solid %3; "
               "border-radius: 6px; color: %4; padding: 3px 2px; "
               "font-size: 10px; }"
               "%1:hover { background: %5; border-color: %6; color: %7; }"
               "%1:checked { background: %8; border-color: %9; "
               "color: %10; font-weight: 600; }")
        .arg(objectSelector(QStringLiteral("QToolButton"), objectName),
             t.sideRail.buttonBackground.name(QColor::HexArgb),
             t.sideRail.buttonBorder.name(QColor::HexArgb),
             t.sideRail.text.name(),
             t.sideRail.buttonHoverBackground.name(),
             t.sideRail.buttonHoverBorder.name(),
             t.sideRail.textHover.name(),
             t.sideRail.buttonCheckedBackground.name(),
             t.sideRail.buttonCheckedBorder.name(),
             t.sideRail.textChecked.name());
}

QString InsightVisualStyle::packageToolsBarStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  border-bottom: 1px solid %3;"
               "}"
               "QToolButton {"
               "  color: %4;"
               "  padding: 3px 6px;"
               "  border: 1px solid transparent;"
               "}"
               "QToolButton:hover {"
               "  background: %5;"
               "  border-color: %6;"
               "}"
               "QToolButton:disabled { color: %7; }")
        .arg(objectSelector(QStringLiteral("QWidget"), objectName),
             t.toolbarBackground.name(),
             t.border.name(),
             t.textPrimary.name(),
             t.graph.nodeHoverFill.name(),
             t.button.borderHover.name(),
             t.button.textDisabled.name());
}

QString InsightVisualStyle::labelStyleSheet(const QString& objectName,
                                            bool strong)
{
    const InsightTheme t = theme();
    return QStringLiteral("%1 { color: %2; %3 }")
        .arg(objectSelector(QStringLiteral("QLabel"), objectName),
             strong ? t.textPrimary.name() : t.textMuted.name(),
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
               "  border-radius: 6px;"
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
               "  border-radius: 6px;"
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
               "  border-radius: 5px;"
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

QString InsightVisualStyle::foldShelfActiveStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    QColor background = t.statusBar.warningBackground;
    QColor border = t.statusBar.warningBorder;
    background.setAlpha(64);
    border.setAlpha(180);
    const QString selector =
        objectSelector(QStringLiteral("QWidget"), objectName);
    return QStringLiteral(
               "%1 {"
               "  border: 2px solid %2;"
               "  background: %3;"
               "}"
               "%1 QListWidget#foldShelfListWidget {"
               "  border: 1px solid %4;"
               "  selection-background-color: %5;"
               "}")
        .arg(selector,
             t.statusBar.warningBorder.name(),
             background.name(QColor::HexArgb),
             border.name(QColor::HexArgb),
             t.statusBar.warningBorder.name());
}

QString InsightVisualStyle::graphViewStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  border: 1px solid %3;"
               "  border-radius: 6px;"
               "}")
        .arg(objectSelector(QStringLiteral("QGraphicsView"), objectName),
             t.graph.background.name(),
             t.border.name());
}

QString InsightVisualStyle::titleBarStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  color: %2;"
               "  background: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 6px;"
               "  padding: 5px 8px;"
               "  font-weight: 600;"
               "}")
        .arg(objectSelector(QStringLiteral("QLabel"), objectName),
             t.textPrimary.name(),
             t.panelSubtle.name(),
             t.border.name());
}

QString InsightVisualStyle::compactSearchFieldStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  color: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 6px;"
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
             t.accent.name());
}

QString InsightVisualStyle::segmentedCheckBoxStyleSheet(
    const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  color: %2;"
               "  spacing: 4px;"
               "  padding: 3px 7px;"
               "  min-height: 22px;"
               "}"
               "%1:hover {"
               "  background: %3;"
               "  border-radius: 6px;"
               "}"
               "%1::indicator {"
               "  width: 14px;"
               "  height: 14px;"
               "}"
               "%1::indicator:unchecked {"
               "  border: 1px solid %4;"
               "  background: %5;"
               "  border-radius: 4px;"
               "}"
               "%1::indicator:checked {"
               "  border: 1px solid %6;"
               "  background: %6;"
               "  border-radius: 4px;"
               "}")
        .arg(objectSelector(QStringLiteral("QCheckBox"), objectName),
             t.textSecondary.name(),
             t.hover.lighter(190).name(),
             t.borderStrong.name(),
             t.panelBackground.name(),
             t.accent.name());
}

QString InsightVisualStyle::toolbarButtonStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  color: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 6px;"
               "  padding: 4px 9px;"
               "  min-height: 22px;"
               "}"
               "%1:hover {"
               "  background: %5;"
               "  border-color: %6;"
               "}"
               "%1:pressed, %1:checked {"
               "  background: %7;"
               "  color: white;"
               "  border-color: %7;"
               "}"
               "%1:disabled {"
               "  background: %8;"
               "  color: %9;"
               "  border-color: %4;"
               "}")
        .arg(objectSelector(QStringLiteral("QPushButton"), objectName),
             t.panelBackground.name(),
             t.textSecondary.name(),
             t.border.name(),
             t.panelSubtle.name(),
             t.borderStrong.name(),
             t.accent.name(),
             t.panelSubtle.name(),
             t.textMuted.name());
}

void InsightVisualStyle::applyPanel(QWidget* widget)
{
    if (!widget)
        return;
    widget->setStyleSheet(panelStyleSheet(widget->objectName()));
}

void InsightVisualStyle::applyTitleLabel(QLabel* label)
{
    if (!label)
        return;
    label->setFont(titleFont(label->font()));
    label->setMinimumHeight(30);
    label->setStyleSheet(titleBarStyleSheet(label->objectName()));
}

void InsightVisualStyle::applySearchField(QLineEdit* edit)
{
    if (!edit)
        return;
    edit->setFont(compactFont(edit->font()));
    edit->setMinimumHeight(28);
    edit->setMinimumWidth(180);
    edit->setStyleSheet(compactSearchFieldStyleSheet(edit->objectName()));
}

void InsightVisualStyle::applyToolbarButton(QPushButton* button)
{
    if (!button)
        return;
    button->setMinimumHeight(28);
    button->setStyleSheet(toolbarButtonStyleSheet(button->objectName()));
}

void InsightVisualStyle::applySegmentedCheckBox(QWidget* checkBox)
{
    if (!checkBox)
        return;
    checkBox->setMinimumHeight(28);
    checkBox->setStyleSheet(segmentedCheckBoxStyleSheet(
        checkBox->objectName()));
}
