#ifndef INSIGHTVISUALSTYLE_H
#define INSIGHTVISUALSTYLE_H

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPen>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;
class QWidget;

enum class InsightVisualRole {
    Write,
    Read,
    Port,
    Condition,
    Case,
    Timing,
    Unknown,
    Kernel,
    Input,
    Output,
    Data,
    Control
};

struct InsightTheme {
    QColor appBackground;
    QColor canvasBackground;
    QColor panelBackground;
    QColor panelSubtle;
    QColor border;
    QColor borderStrong;
    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;
    QColor accent;
    QColor selected;
    QColor hover;
    QColor warning;
};

class InsightVisualStyle
{
public:
    static InsightTheme theme();

    static QColor roleColor(InsightVisualRole role);
    static QColor roleColor(const QString& roleName);
    static QColor roleFillColor(InsightVisualRole role);
    static QColor heatIntensityColor(double intensity);

    static QPen hairlinePen(const QColor& color);
    static QPen panelBorderPen();
    static QPen rolePen(InsightVisualRole role, qreal width = 1.4);
    static QPen selectedPen(qreal width = 2.2);
    static QPen hoverPen(qreal width = 1.8);
    static QBrush panelBrush();
    static QBrush canvasBrush();

    static QFont titleFont(const QFont& base);
    static QFont compactFont(const QFont& base);
    static QFont labelFont(const QFont& base);

    static QString panelStyleSheet(const QString& objectName = {});
    static QString titleBarStyleSheet(const QString& objectName = {});
    static QString compactSearchFieldStyleSheet(
        const QString& objectName = {});
    static QString toolbarButtonStyleSheet(
        const QString& objectName = {});
    static QString segmentedCheckBoxStyleSheet(
        const QString& objectName = {});
    static QString inspectorCardStyleSheet(const QString& objectName = {});
    static QString legendSwatchStyleSheet(const QColor& color);

    static void applyPanel(QWidget* widget);
    static void applyTitleLabel(QLabel* label);
    static void applySearchField(QLineEdit* edit);
    static void applyToolbarButton(QPushButton* button);
    static void applySegmentedCheckBox(QWidget* checkBox);
};

#endif // INSIGHTVISUALSTYLE_H
