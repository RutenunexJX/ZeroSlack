#include "uitypography.h"
#include "workspacehubview.h"

#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "roundedicons.h"
#include "semanticstateview.h"
#include "workspacehubmodel.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

WorkspaceHubView::WorkspaceHubView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workspaceHubView"));
    setAccessibleName(QStringLiteral("Workspace Hub"));
    setMinimumWidth(340);
    const InsightTheme& initialTheme = InsightVisualStyle::theme();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        initialTheme.density.panelHeaderPadding,
        initialTheme.density.panelHeaderPadding,
        initialTheme.density.panelHeaderPadding,
        initialTheme.density.panelHeaderPadding);
    layout->setSpacing(initialTheme.density.baseSpacing + 2);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("workspaceHubHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 2, 2);
    headerLayout->setSpacing(6);
    auto* title = new QLabel(QStringLiteral("Workspace Hub"), header);
    title->setObjectName(QStringLiteral("workspaceHubTitle"));
    UiTypography::apply(title, UiTypography::Role::PanelTitle);
    title->setAccessibleName(QStringLiteral("Workspace Hub"));
    title->setMinimumWidth(70);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusChip = new QLabel(QStringLiteral("Idle"), header);
    statusChip->setObjectName(QStringLiteral("workspaceHubStatus"));
    UiTypography::apply(statusChip, UiTypography::Role::Badge);
    statusChip->setAccessibleName(QStringLiteral("Workspace Hub status"));
    statusChip->setTextFormat(Qt::PlainText);
    statusChip->setMaximumWidth(96);
    statusChip->setAlignment(Qt::AlignCenter);
    previewButton = new QToolButton(header);
    previewButton->setObjectName(QStringLiteral("workspaceHubPreviewToggle"));
    previewButton->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
    previewButton->setIconSize(QSize(16, 16));
    previewButton->setToolTip(QStringLiteral("Show or hide item preview"));
    previewButton->setAccessibleName(QStringLiteral("Toggle item preview"));
    previewButton->setCheckable(true);
    previewButton->setChecked(true);
    previewButton->setFocusPolicy(Qt::StrongFocus);
    previewButton->setMinimumWidth(32);
    previewButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    refreshButton = new QToolButton(header);
    refreshButton->setObjectName(QStringLiteral("workspaceHubRefresh"));
    refreshButton->setIcon(RoundedIcons::icon(RoundedIcons::Refresh));
    refreshButton->setIconSize(QSize(16, 16));
    refreshButton->setToolTip(QStringLiteral("Refresh Workspace Hub"));
    refreshButton->setAccessibleName(QStringLiteral("Refresh Workspace Hub"));
    refreshButton->setFocusPolicy(Qt::StrongFocus);
    refreshButton->setMinimumWidth(32);
    refreshButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    headerLayout->addWidget(title, 1);
    headerLayout->addWidget(statusChip);
    headerLayout->addWidget(previewButton);
    headerLayout->addWidget(refreshButton);
    layout->addWidget(header);

    hubModel = new WorkspaceHubModel(this);
    tree = new QTreeView(this);
    tree->setObjectName(QStringLiteral("workspaceHubTree"));
    tree->setAccessibleName(QStringLiteral("Workspace resources"));
    tree->setAccessibleDescription(QStringLiteral(
        "Source, Pinloom, Wave, and RegMap resources for the active workspace."));
    tree->setModel(hubModel);
    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    tree->setAnimated(false);
    tree->setExpandsOnDoubleClick(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setFocusPolicy(Qt::StrongFocus);
    tree->installEventFilter(this);
    tree->viewport()->installEventFilter(this);
    layout->addWidget(tree, 1);

    semanticState = new SemanticStateView(this);
    semanticState->setObjectName(QStringLiteral("workspaceHubSemanticState"));
    layout->addWidget(semanticState);

    preview = new QFrame(this);
    preview->setObjectName(QStringLiteral("workspaceHubPreview"));
    auto* previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(10, 8, 10, 8);
    previewLayout->setSpacing(3);
    previewTitle = new QLabel(preview);
    previewTitle->setObjectName(QStringLiteral("workspaceHubPreviewTitle"));
    previewTitle->setWordWrap(true);
    previewTitle->setTextFormat(Qt::PlainText);
    previewSummary = new QLabel(preview);
    previewSummary->setObjectName(QStringLiteral("workspaceHubPreviewSummary"));
    previewSummary->setWordWrap(true);
    previewSummary->setTextFormat(Qt::PlainText);
    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(previewSummary);
    preview->setVisible(false);
    layout->addWidget(preview);

    connect(refreshButton, &QToolButton::clicked,
            this, &WorkspaceHubView::refreshRequested);
    connect(previewButton, &QToolButton::toggled,
            this, [this](bool checked) {
                previewEnabled = checked;
                updatePreview();
                if (!applyingState)
                    emit viewStateChanged();
            });
    connect(tree, &QTreeView::expanded, this,
            [this](const QModelIndex&) {
                if (!applyingState)
                    emit viewStateChanged();
            });
    connect(tree, &QTreeView::collapsed, this,
            [this](const QModelIndex&) {
                if (!applyingState)
                    emit viewStateChanged();
            });
    connect(tree->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex&, const QModelIndex&) {
                updatePreview();
                if (!applyingState)
                    emit viewStateChanged();
            });
    connect(tree->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) {
                if (!applyingState)
                    emit viewStateChanged();
            });
    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this, [this](ThemeMode) { refreshPresentation(); });
    refreshPresentation();
}

WorkspaceHubModel* WorkspaceHubView::model() const
{
    return hubModel;
}

QTreeView* WorkspaceHubView::treeView() const
{
    return tree;
}

SemanticStateView* WorkspaceHubView::stateView() const
{
    return semanticState;
}

void WorkspaceHubView::setSnapshot(
    const WorkspaceHubSnapshot& snapshot)
{
    const bool hadSections = hubModel->rowCount() > 0;
    const QString selected = tree->currentIndex().data(
        WorkspaceHubModel::StableKeyRole).toString();
    QStringList expanded;
    for (int row = 0; row < hubModel->rowCount(); ++row) {
        const QModelIndex section = hubModel->index(row, 0);
        if (tree->isExpanded(section))
            expanded.append(section.data(
                WorkspaceHubModel::SectionIdRole).toString());
    }
    hubModel->setSnapshot(snapshot);
    for (int row = 0; row < hubModel->rowCount(); ++row) {
        const QModelIndex section = hubModel->index(row, 0);
        const QString id = section.data(
            WorkspaceHubModel::SectionIdRole).toString();
        tree->setExpanded(section,
                          !hadSections || expanded.contains(id));
    }
    QModelIndex restoreSelection =
        hubModel->indexForStableKey(selected);
    if (!restoreSelection.isValid() && hubModel->rowCount() > 0) {
        for (int section = 0; section < hubModel->rowCount(); ++section) {
            const QModelIndex group = hubModel->index(section, 0);
            if (hubModel->rowCount(group) > 0) {
                restoreSelection = hubModel->index(0, 0, group);
                break;
            }
        }
    }
    if (restoreSelection.isValid())
        tree->setCurrentIndex(restoreSelection);

    int itemCount = 0;
    for (const WorkspaceHubSection& section : snapshot.sections)
        itemCount += section.items.size();
    if (snapshot.phase == WorkspaceHubPhase::Updating) {
        statusChip->setText(snapshot.stale
                                ? QStringLiteral("Updating · stale")
                                : QStringLiteral("Updating"));
        semanticState->setState(
            snapshot.stale ? SemanticStateKind::Stale
                           : SemanticStateKind::Loading,
            snapshot.stale ? QStringLiteral("Refreshing context")
                           : QStringLiteral("Loading workspace context"),
            snapshot.stale
                ? QStringLiteral("The previous result remains visible until the new generation completes.")
                : QString());
        semanticState->setVisible(!snapshot.stale || itemCount == 0);
    } else if (snapshot.phase == WorkspaceHubPhase::Error) {
        statusChip->setText(QStringLiteral("Error"));
        semanticState->setState(
            SemanticStateKind::Error,
            QStringLiteral("Workspace context is unavailable"),
            snapshot.failureReason,
            QStringLiteral("Retry"));
        semanticState->setVisible(true);
    } else if (itemCount == 0) {
        statusChip->setText(QStringLiteral("Empty"));
        semanticState->setState(
            SemanticStateKind::Empty,
            QStringLiteral("No workspace context"),
            QStringLiteral("Open an RTL file or add .zeroslack/suite-references.json."),
            QStringLiteral("Refresh"));
        semanticState->setVisible(true);
    } else {
        statusChip->setText(QStringLiteral("%1 items").arg(itemCount));
        semanticState->setVisible(false);
    }
    statusChip->setAccessibleName(
        QStringLiteral("Workspace Hub status: %1")
            .arg(statusChip->text()));
    statusChip->setToolTip(statusChip->text());
    applyPendingState();
    updatePreview();
}

QVariantMap WorkspaceHubView::saveState() const
{
    if (hubModel->rowCount() == 0 && !pendingState.isEmpty())
        return pendingState;
    QStringList expanded;
    for (int row = 0; row < hubModel->rowCount(); ++row) {
        const QModelIndex section = hubModel->index(row, 0);
        if (tree->isExpanded(section)) {
            expanded.append(section.data(
                WorkspaceHubModel::SectionIdRole).toString());
        }
    }
    return {
        {QStringLiteral("expandedSections"), expanded},
        {QStringLiteral("selectedItem"), tree->currentIndex().data(
             WorkspaceHubModel::StableKeyRole).toString()},
        {QStringLiteral("previewVisible"), previewEnabled},
        {QStringLiteral("scrollValue"),
         tree->verticalScrollBar()->value()},
    };
}

void WorkspaceHubView::restoreState(const QVariantMap& state)
{
    pendingState = state;
    applyPendingState();
}

bool WorkspaceHubView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == tree->viewport()
        && event->type() == QEvent::MouseButtonDblClick) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        activateIndex(tree->indexAt(mouseEvent->position().toPoint()));
        return true;
    }
    if (watched == tree && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter) {
            activateIndex(tree->currentIndex());
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape && preview->isVisible()) {
            previewButton->setChecked(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WorkspaceHubView::activateIndex(const QModelIndex& index)
{
    const WorkspaceHubItem item = hubModel->itemForIndex(index);
    if (!item.isValid())
        return;
    if (item.state == WorkspaceHubItemState::Missing
        || item.state == WorkspaceHubItemState::Unavailable) {
        semanticState->setState(
            SemanticStateKind::Warning,
            QStringLiteral("Resource unavailable"),
            item.summary.trimmed().isEmpty()
                ? QStringLiteral("The selected resource cannot be opened in its provider.")
                : item.summary,
            QStringLiteral("Refresh"));
        semanticState->setVisible(true);
        return;
    }
    emit itemActivated(item);
}

void WorkspaceHubView::updatePreview()
{
    const WorkspaceHubItem item = hubModel->itemForIndex(
        tree->currentIndex());
    if (!item.isValid()) {
        preview->setVisible(false);
        return;
    }
    previewTitle->setText(item.title);
    QStringList rows;
    if (!item.summary.trimmed().isEmpty())
        rows.append(item.summary);
    const QString surface = item.metadata.value(
        QStringLiteral("surfacePreview")).toString().trimmed();
    if (!surface.isEmpty())
        rows.append(surface);
    rows.append(QStringLiteral("State: %1")
                    .arg(workspaceHubStateId(item.state)));
    previewSummary->setText(rows.join(QLatin1Char('\n')));
    preview->setVisible(previewEnabled);
    preview->setAccessibleName(item.title);
    preview->setAccessibleDescription(previewSummary->text());
}

void WorkspaceHubView::applyPendingState()
{
    if (pendingState.isEmpty() || hubModel->rowCount() == 0)
        return;
    applyingState = true;
    const QStringList expanded = pendingState.value(
        QStringLiteral("expandedSections")).toStringList();
    const bool hasExpandedState = pendingState.contains(
        QStringLiteral("expandedSections"));
    for (int row = 0; row < hubModel->rowCount(); ++row) {
        const QModelIndex section = hubModel->index(row, 0);
        tree->setExpanded(section, !hasExpandedState
            || expanded.contains(section.data(
                WorkspaceHubModel::SectionIdRole).toString()));
    }
    const QModelIndex selected = hubModel->indexForStableKey(
        pendingState.value(QStringLiteral("selectedItem")).toString());
    if (selected.isValid())
        tree->setCurrentIndex(selected);
    tree->verticalScrollBar()->setValue(
        qMax(0, pendingState.value(
            QStringLiteral("scrollValue")).toInt()));
    previewEnabled = pendingState.value(
        QStringLiteral("previewVisible"), true).toBool();
    {
        const QSignalBlocker blocker(previewButton);
        previewButton->setChecked(previewEnabled);
    }
    preview->setVisible(previewEnabled);
    pendingState.clear();
    applyingState = false;
}

void WorkspaceHubView::refreshPresentation()
{
    const InsightTheme& theme = InsightVisualStyle::theme();
    setStyleSheet(QStringLiteral(
        "QWidget#workspaceHubView { background: %1; color: %2; }"
        "QWidget#workspaceHubHeader { background: %3; border: 1px solid %4; "
        "border-radius: %8px; }"
        "QLabel#workspaceHubTitle { color: %2; font-weight: %9; }"
        "QLabel#workspaceHubStatus { color: %5; padding: 2px 6px; "
        "background: %6; border-radius: 6px; }"
        "QTreeView#workspaceHubTree { background: %1; border: 1px solid %4; "
        "border-radius: %8px; padding: 3px; outline: 0; }"
        "QTreeView#workspaceHubTree:focus { border: %10px solid %7; }"
        "QFrame#workspaceHubPreview { background: %3; border: 1px solid %4; "
        "border-radius: %8px; }"
        "QLabel#workspaceHubPreviewTitle { color: %2; font-weight: %9; }"
        "QLabel#workspaceHubPreviewSummary { color: %5; }")
        .arg(theme.panelBackground.name(), theme.textPrimary.name(),
             theme.panelSubtle.name(), theme.border.name(),
             theme.textSecondary.name(),
             theme.statusBar.infoBackground.name(),
             theme.focus.ring.name())
        .arg(theme.density.smallRadius)
        .arg(theme.typography.panelTitleWeight)
        .arg(theme.focus.width));
}
