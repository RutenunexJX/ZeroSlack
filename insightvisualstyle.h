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

enum class InsightStatusTone {
    Info,
    Success,
    Warning,
    Error
};

struct InsightControlTokens {
    QColor background;
    QColor backgroundHover;
    QColor backgroundPressed;
    QColor backgroundChecked;
    QColor text;
    QColor textHover;
    QColor textChecked;
    QColor textDisabled;
    QColor border;
    QColor borderHover;
    QColor borderChecked;
};

struct InsightTabTokens {
    QColor barBackground;
    QColor tabBackground;
    QColor tabBackgroundHover;
    QColor tabBackgroundSelected;
    QColor text;
    QColor textHover;
    QColor textSelected;
    QColor border;
    QColor borderSelected;
};

struct InsightMenuTokens {
    QColor background;
    QColor itemHoverBackground;
    QColor text;
    QColor itemHoverText;
    QColor border;
};

struct InsightRailTokens {
    QColor background;
    QColor border;
    QColor buttonBackground;
    QColor buttonHoverBackground;
    QColor buttonCheckedBackground;
    QColor buttonBorder;
    QColor buttonHoverBorder;
    QColor buttonCheckedBorder;
    QColor text;
    QColor textHover;
    QColor textChecked;
};

struct InsightDockTokens {
    QColor background;
    QColor titleBackground;
    QColor border;
    QColor titleBorder;
    QColor text;
};

struct InsightInputTokens {
    QColor background;
    QColor text;
    QColor border;
    QColor focusBorder;
    QColor selectionBackground;
};

struct InsightItemViewTokens {
    QColor background;
    QColor alternateBackground;
    QColor headerBackground;
    QColor border;
    QColor headerBorder;
    QColor text;
    QColor headerText;
    QColor selectedBackground;
    QColor hoverBackground;
};

struct InsightStatusTokens {
    QColor background;
    QColor text;
    QColor border;
    QColor infoText;
    QColor infoBackground;
    QColor infoBorder;
    QColor successText;
    QColor successBackground;
    QColor successBorder;
    QColor warningText;
    QColor warningBackground;
    QColor warningBorder;
    QColor errorText;
    QColor errorBackground;
    QColor errorBorder;
};

struct InsightGraphTokens {
    QColor background;
    QColor gridLine;
    QColor nodeFill;
    QColor nodeBorder;
    QColor nodeHoverFill;
    QColor nodeHoverBorder;
    QColor nodeSelectedFill;
    QColor nodeSelectedBorder;
    QColor edge;
    QColor edgeHover;
    QColor edgeSelected;
    QColor selectionFill;
    QColor selectionBorder;
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
    QColor splitterHandle;
    QColor toolbarBackground;

    InsightMenuTokens menu;
    InsightControlTokens button;
    InsightInputTokens input;
    InsightTabTokens tab;
    InsightRailTokens sideRail;
    InsightStatusTokens statusBar;
    InsightDockTokens dock;
    InsightItemViewTokens itemView;
    InsightGraphTokens graph;
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
    static QString applicationStyleSheet();
    static QString workspaceTabBarStyleSheet(const QString& objectName = {});
    static QString sideRailStyleSheet(const QString& objectName = {});
    static QString sideRailButtonStyleSheet(const QString& objectName = {});
    static QString packageToolsBarStyleSheet(const QString& objectName = {});
    static QString labelStyleSheet(const QString& objectName = {},
                                   bool strong = false);
    static QString statusChipStyleSheet(
        InsightStatusTone tone = InsightStatusTone::Info,
        const QString& objectName = {});
    static QString dockAttentionStyleSheet(
        const QString& objectName,
        InsightStatusTone tone = InsightStatusTone::Warning);
    static QString globalControlPanelStyleSheet(
        const QString& objectName = {});
    static QString foldShelfActiveStyleSheet(
        const QString& objectName = {});
    static QString graphViewStyleSheet(const QString& objectName = {});
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
