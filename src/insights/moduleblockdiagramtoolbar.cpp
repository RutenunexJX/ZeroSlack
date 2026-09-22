#include "moduleblockdiagramtoolbar.h"

#include "applicationthememanager.h"
#include "roundedicons.h"
#include "uicontrols.h"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaBreadcrumbBar.h"
#include "ElaToolBar.h"
#endif

ModuleBlockDiagramToolbar::ModuleBlockDiagramToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("rtlModuleBlockToolbar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    row = new QGridLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    column->addLayout(row);

    navigation = new QWidget(this);
    navigation->setMinimumWidth(0);
    auto* path = new QHBoxLayout(navigation);
    path->setContentsMargins(0, 0, 0, 0);
    path->setSpacing(2);
    auto* back = UiControls::toolButton(navigation);
    backAction = new QAction(RoundedIcons::icon(RoundedIcons::Left), tr("Back to parent module"), this);
    backAction->setObjectName(QStringLiteral("rtlModuleBackAction"));
    back->setDefaultAction(backAction);
    back->setToolButtonStyle(Qt::ToolButtonIconOnly);
    back->setIconSize(QSize(16, 16));
    back->setAutoRaise(true);
    path->addWidget(back);

#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        auto* elaPath = new ElaBreadcrumbBar(navigation);
        elaPath->setTextPixelSize(qMax(12, fontMetrics().height() - 2));
        elaPath->setIsAutoRemove(true);
        connect(elaPath, &ElaBreadcrumbBar::breadcrumbClicked, this,
                [this, elaPath](const QString&, const QStringList&) {
                    // The count identifies the clicked ancestor even when labels repeat.
                    if (breadcrumbActivated)
                        breadcrumbActivated(elaPath->getBreadcrumbListCount() - 1);
                });
        breadcrumbs = elaPath;
        auto* elaTools = new ElaToolBar(this);
        elaTools->setToolBarSpacing(1);
        elaTools->setToolButtonSize(QSize(28, 28));
        tools = elaTools;
    }
#endif
    if (!breadcrumbs) {
        auto* label = UiControls::label(navigation);
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        connect(label, &QLabel::linkActivated, this, [this](const QString& link) {
            bool valid = false;
            const int index = link.toInt(&valid);
            if (valid && breadcrumbActivated) breadcrumbActivated(index);
        });
        breadcrumbs = label;
    }
    breadcrumbs->setObjectName(QStringLiteral("rtlModuleBreadcrumbs"));
    breadcrumbs->setMinimumWidth(0);
    breadcrumbs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    path->addWidget(breadcrumbs, 1);
    if (!tools) tools = new QToolBar(this);
    tools->setObjectName(QStringLiteral("rtlModuleToolBarActions"));
    tools->setMovable(false);
    tools->setFloatable(false);
    tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tools->setIconSize(QSize(16, 16));
    tools->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    const auto addAction = [this](RoundedIcons::Kind icon, const QString& title, const QString& name) {
        auto* action = new QAction(RoundedIcons::icon(icon), title, this);
        action->setObjectName(name);
        // ElaToolButton keeps its native hover/focus treatment inside ElaToolBar.
        auto* button = UiControls::toolButton(tools);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        tools->addWidget(button);
        return action;
    };
    searchAction = addAction(RoundedIcons::Search, tr("Search instances or modules"), QStringLiteral("rtlModuleSearchAction"));
    searchAction->setCheckable(true);
    fitAction = addAction(RoundedIcons::Expand, tr("Fit diagram"), QStringLiteral("rtlModuleFitAction"));
    zoomOutAction = addAction(RoundedIcons::ZoomOut, tr("Zoom out"), QStringLiteral("rtlModuleZoomOutAction"));
    zoom = UiControls::toolButton(tools);
    zoom->setText(QStringLiteral("100%"));
    zoom->setToolTip(tr("Fit diagram (up to 100%)"));
    zoom->setAutoRaise(true);
    tools->addWidget(zoom);
    connect(zoom, &QToolButton::clicked, fitAction, &QAction::trigger);
    zoomInAction = addAction(RoundedIcons::ZoomIn, tr("Zoom in"), QStringLiteral("rtlModuleZoomInAction"));
    foldAction = addAction(RoundedIcons::Collapse, tr("Collapse selected module"), QStringLiteral("rtlModuleFoldAction"));
    more = UiControls::toolButton(tools);
    more->setObjectName(QStringLiteral("rtlModuleMoreButton"));
    more->setIcon(RoundedIcons::icon(RoundedIcons::More));
    more->setToolTip(tr("Module actions and display options"));
    more->setPopupMode(QToolButton::InstantPopup);
    more->setAutoRaise(true);
    tools->addWidget(more);
    row->addWidget(navigation, 0, 0);
    row->addWidget(tools, 0, 1);
    row->setColumnStretch(0, 1);

    search = UiControls::lineEdit(this);
    search->setObjectName(QStringLiteral("rtlModuleSearchEdit"));
    search->setPlaceholderText(tr("Search instances or modules…"));
    search->setClearButtonEnabled(true);
    search->hide();
    column->addWidget(search);
    connect(searchAction, &QAction::toggled, this, [this](bool visible) {
        search->setVisible(visible);
        if (visible) search->setFocus();
    });
    connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (searchChanged) searchChanged(text);
    });
}

void ModuleBlockDiagramToolbar::setBreadcrumbs(const QStringList& labels)
{
    pathLabels = labels;
    backAction->setEnabled(labels.size() > 1);
    breadcrumbs->setToolTip(labels.join(QStringLiteral(" / ")));
#ifdef ZEROSLACK_ENABLE_ELA
    if (auto* bar = qobject_cast<ElaBreadcrumbBar*>(breadcrumbs)) {
        if (bar->getBreadcrumbList() != labels) bar->setBreadcrumbList(labels);
        return;
    }
#endif
    QStringList links;
    for (int i = 0; i < labels.size(); ++i) {
        const QString label = labels.at(i).toHtmlEscaped();
        links.append(i + 1 == labels.size() ? label
            : QStringLiteral("<a href=\"%1\">%2</a>").arg(i).arg(label));
    }
    qobject_cast<QLabel*>(breadcrumbs)->setText(links.join(QStringLiteral(" &nbsp;›&nbsp; ")));
}

void ModuleBlockDiagramToolbar::setSearchText(const QString& text)
{
    const QSignalBlocker blocker(search);
    search->setText(text);
}

void ModuleBlockDiagramToolbar::setMoreMenu(QMenu* menu) { more->setMenu(menu); }
void ModuleBlockDiagramToolbar::setZoom(qreal scale) { zoom->setText(QStringLiteral("%1%").arg(qRound(scale * 100))); }
void ModuleBlockDiagramToolbar::setFoldAvailable(bool enabled, bool collapsed)
{
    foldAction->setEnabled(enabled);
    foldAction->setText(collapsed ? tr("Expand selected module") : tr("Collapse selected module"));
    foldAction->setIcon(RoundedIcons::icon(collapsed ? RoundedIcons::Expand : RoundedIcons::Collapse));
}

void ModuleBlockDiagramToolbar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    arrange();
}

void ModuleBlockDiagramToolbar::arrange()
{
    const bool nextStacked = width() < tools->sizeHint().width() + 240;
    if (nextStacked == stacked) return;
    stacked = nextStacked;
    row->removeWidget(navigation);
    row->removeWidget(tools);
    row->addWidget(navigation, 0, 0, 1, stacked ? 2 : 1);
    row->addWidget(tools, stacked ? 1 : 0, stacked ? 0 : 1, 1, 1, Qt::AlignLeft);
}
