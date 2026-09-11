#ifndef INSIGHTVISUALSTYLE_H
#define INSIGHTVISUALSTYLE_H

#include "applicationthememanager.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPalette>
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
    QColor selectionText;
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

struct InsightSurfaceTokens {
    QColor canvas;
    QColor panel;
    QColor raised;
    QColor overlay;
    QColor empty;
    QColor loading;
    QColor stale;
};

struct InsightDensityTokens {
    int baseSpacing = 4;
    int compactControlHeight = 28;
    int controlHeight = 32;
    int primaryControlHeight = 36;
    int smallRadius = 6;
    int radius = 8;
    int panelHeaderPadding = 8;
};

struct InsightFocusTokens {
    QColor ring;
    QColor ringOnAccent;
    int width = 2;
};

struct InsightTypographyTokens {
    int applicationTitleWeight = 650;
    int panelTitleWeight = 600;
    int bodyWeight = 400;
    int metadataWeight = 400;
};

struct InsightSyntaxTokens {
    QColor keyword;
    QColor comment;
    QColor number;
    QColor string;
    QColor errorUnderline;
    QColor warningUnderline;
    QColor structuralPair;
};

struct InsightSemanticTokens {
    QColor write;
    QColor read;
    QColor port;
    QColor condition;
    QColor caseRole;
    QColor timing;
    QColor unknown;
    QColor kernel;
    QColor data;

    QColor writeFill;
    QColor readFill;
    QColor portFill;
    QColor conditionFill;
    QColor caseFill;
    QColor timingFill;
    QColor unknownFill;
    QColor kernelFill;
    QColor dataFill;

    QColor heatLow;
    QColor heatMid;
    QColor heatHigh;
};

struct InsightEditorSemanticTokens {
    QColor moduleInterface;
    QColor packageClassType;
    QColor instanceName;
    QColor formalPort;
    QColor modulePort;
    QColor actualSignal;
    QColor parameter;
    QColor enumValue;
    QColor typeAlias;
    QColor macro;
    QColor systemTask;
    QColor inactiveText;
    QColor inactiveBackground;
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
    InsightStatusTokens statusBar;
    InsightSurfaceTokens surface;
    InsightDensityTokens density;
    InsightFocusTokens focus;
    InsightTypographyTokens typography;
    InsightDockTokens dock;
    InsightItemViewTokens itemView;
    InsightGraphTokens graph;
    InsightSyntaxTokens syntax;
    InsightSemanticTokens semantic;
    InsightEditorSemanticTokens editorSemantic;
};

class InsightVisualStyle
{
public:
    static const InsightTheme& theme();
    static const InsightTheme& theme(ThemeMode mode);
    static QColor subtleBorder(const QColor& surface, const QColor& foreground);

    static QColor roleColor(InsightVisualRole role);
    static QColor roleColor(InsightVisualRole role, ThemeMode mode);
    static QColor roleColor(const QString& roleName);
    static QColor roleFillColor(InsightVisualRole role);
    static QColor roleFillColor(InsightVisualRole role, ThemeMode mode);
    static QColor heatIntensityColor(double intensity);
    static QColor heatIntensityColor(double intensity, ThemeMode mode);

    static QPen hairlinePen(const QColor& color);
    static QPen panelBorderPen();
    static QPen selectedPen(qreal width = 2.2);
    static QPen hoverPen(qreal width = 1.8);
    static QBrush panelBrush();
    static QBrush canvasBrush();

    static QFont titleFont(const QFont& base);
    static QFont compactFont(const QFont& base);
    static QFont labelFont(const QFont& base);

    static QPalette applicationPalette();
    static QPalette applicationPalette(ThemeMode mode);
    static QString panelStyleSheet(const QString& objectName = {});
    static QString applicationStyleSheet();
    static QString applicationStyleSheet(ThemeMode mode);
    static QString tabBarStyleSheet(const QString& objectName = {});
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
    static QString sideInspectorStyleSheet(
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

    static void applyPanel(QWidget* widget);
    static void applyTitleLabel(QLabel* label);
    static void applyLabel(QLabel* label, bool strong = false);
    static void applySearchField(QLineEdit* edit);
    static void applyToolbarButton(QPushButton* button);
    static void applySegmentedCheckBox(QWidget* checkBox);
    static void applyGlobalControlPanel(QWidget* widget);
    static void applySideInspector(QWidget* widget);
};

#endif // INSIGHTVISUALSTYLE_H
