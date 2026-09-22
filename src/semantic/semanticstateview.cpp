#include "uitypography.h"
#include "uicontrols.h"
#include "semanticstateview.h"

#include "applicationthememanager.h"
#include "insightvisualstyle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SemanticStateView::SemanticStateView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("semanticStateView"));
    setProperty("semanticState", true);
    setAccessibleName(QStringLiteral("Workspace Hub status"));
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(8);
    marker = new QLabel(this);
    marker->setObjectName(QStringLiteral("semanticStateMarker"));
    marker->setFixedSize(10, 10);
    marker->setAccessibleName(QStringLiteral("Status marker"));
    outer->addWidget(marker, 0, Qt::AlignTop);

    auto* textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(6);
    titleLabel = UiControls::label(this);
    titleLabel->setObjectName(QStringLiteral("semanticStateTitle"));
    titleLabel->setWordWrap(true);
    UiTypography::apply(titleLabel, UiTypography::Role::PanelTitle);
    titleLabel->setTextFormat(Qt::PlainText);
    detailLabel = UiControls::label(this);
    detailLabel->setObjectName(QStringLiteral("semanticStateDetail"));
    detailLabel->setWordWrap(true);
    UiTypography::apply(detailLabel, UiTypography::Role::Metadata);
    detailLabel->setTextFormat(Qt::PlainText);
    action = UiControls::pushButton(this);
    action->setObjectName(QStringLiteral("semanticStateAction"));
    action->setVisible(false);
    action->setAutoDefault(false);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(detailLabel);
    textLayout->addWidget(action, 0, Qt::AlignLeft);
    outer->addLayout(textLayout, 1);
    connect(action, &QPushButton::clicked,
            this, &SemanticStateView::actionRequested);
    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this, [this](ThemeMode) { refreshStyle(); });
    setState(SemanticStateKind::Empty,
             QStringLiteral("No items"));
}

void SemanticStateView::setState(
    SemanticStateKind kind,
    const QString& title,
    const QString& detail,
    const QString& actionText)
{
    kindValue = kind;
    titleLabel->setText(title);
    detailLabel->setText(detail);
    detailLabel->setVisible(!detail.trimmed().isEmpty());
    action->setText(actionText);
    action->setAccessibleName(actionText);
    action->setVisible(!actionText.trimmed().isEmpty());
    setAccessibleDescription(
        detail.trimmed().isEmpty()
            ? title : QStringLiteral("%1. %2").arg(title, detail));
    refreshStyle();
}

SemanticStateKind SemanticStateView::stateKind() const
{
    return kindValue;
}

QString SemanticStateView::titleText() const
{
    return titleLabel->text();
}

QString SemanticStateView::detailText() const
{
    return detailLabel->text();
}

QPushButton* SemanticStateView::actionButton() const
{
    return action;
}

void SemanticStateView::refreshStyle()
{
    const InsightTheme& theme = InsightVisualStyle::theme();
    QColor tone = theme.textMuted;
    QColor background = theme.surface.empty;
    if (kindValue == SemanticStateKind::Loading) {
        tone = theme.accent;
        background = theme.surface.loading;
    } else if (kindValue == SemanticStateKind::Stale) {
        tone = theme.statusBar.warningText;
        background = theme.surface.stale;
    } else if (kindValue == SemanticStateKind::Warning) {
        tone = theme.statusBar.warningText;
        background = theme.statusBar.warningBackground;
    } else if (kindValue == SemanticStateKind::Error) {
        tone = theme.statusBar.errorText;
        background = theme.statusBar.errorBackground;
    }
    setStyleSheet(QStringLiteral(
        "QWidget[semanticState=\"true\"] { background: %1; border: 1px solid %2; "
        "border-radius: %7px; }"
        "QLabel#semanticStateMarker { background: %3; border-radius: 5px; }"
        "QLabel#semanticStateTitle { color: %4; font-weight: %8; }"
        "QLabel#semanticStateDetail { color: %5; }"
        "QPushButton#semanticStateAction:focus { border: %9px solid %6; }")
        .arg(background.name(), theme.border.name(), tone.name(),
             theme.textPrimary.name(), theme.textSecondary.name(),
             theme.focus.ring.name())
        .arg(theme.density.smallRadius)
        .arg(theme.typography.panelTitleWeight)
        .arg(theme.focus.width));
}
