#include "insightvisualstyle.h"

#include <QLabel>
#include <QLineEdit>
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

InsightTheme InsightVisualStyle::theme()
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
    return theme;
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

QPen InsightVisualStyle::rolePen(InsightVisualRole role, qreal width)
{
    return QPen(roleColor(role), width);
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

QString InsightVisualStyle::inspectorCardStyleSheet(const QString& objectName)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "%1 {"
               "  background: %2;"
               "  border: 1px solid %3;"
               "  border-radius: 8px;"
               "  color: %4;"
               "}")
        .arg(objectSelector(QStringLiteral("QWidget"), objectName),
             t.panelBackground.name(),
             t.border.name(),
             t.textPrimary.name());
}

QString InsightVisualStyle::legendSwatchStyleSheet(const QColor& color)
{
    const InsightTheme t = theme();
    return QStringLiteral(
               "background: %1;"
               "border: 1px solid %2;"
               "border-radius: 3px;"
               "min-width: 10px;"
               "max-width: 10px;"
               "min-height: 10px;"
               "max-height: 10px;")
        .arg(color.name(), t.border.name());
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

void InsightVisualStyle::applySegmentedCheckBox(QWidget* checkBox)
{
    if (!checkBox)
        return;
    checkBox->setMinimumHeight(28);
    checkBox->setStyleSheet(segmentedCheckBoxStyleSheet(
        checkBox->objectName()));
}
