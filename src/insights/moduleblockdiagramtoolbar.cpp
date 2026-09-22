#include "moduleblockdiagramtoolbar.h"

#include "applicationthememanager.h"
#include "roundedicons.h"
#include "uicontrols.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaBreadcrumbBar.h"
#endif

ModuleBlockDiagramToolbar::ModuleBlockDiagramToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("rtlModuleBlockToolbar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    const auto addButton = [this, row](RoundedIcons::Kind icon, const QString& title,
                                      const QString& name, QAction*& action) {
        action = new QAction(RoundedIcons::icon(icon), title, this);
        action->setObjectName(name);
        auto* button = UiControls::toolButton(this);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        row->addWidget(button);
        return button;
    };
    addButton(RoundedIcons::Left, tr("Back"), QStringLiteral("rtlModuleBackAction"), backAction);
    addButton(RoundedIcons::Right, tr("Forward"), QStringLiteral("rtlModuleForwardAction"), forwardAction);
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        auto* bar = new ElaBreadcrumbBar(this);
        bar->setTextPixelSize(qMax(12, fontMetrics().height() - 2));
        bar->setIsAutoRemove(true);
        connect(bar, &ElaBreadcrumbBar::breadcrumbClicked, this,
                [this, bar](const QString&, const QStringList&) {
                    if (breadcrumbActivated) breadcrumbActivated(bar->getBreadcrumbListCount() - 1);
                });
        breadcrumbs = bar;
    }
#endif
    if (!breadcrumbs) {
        auto* label = UiControls::label(this);
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
    row->addWidget(breadcrumbs, 1);
    fitButton = addButton(RoundedIcons::Expand, tr("Fit diagram"), QStringLiteral("rtlModuleFitAction"), fitAction);
    setHistoryAvailable(false, false);
}

void ModuleBlockDiagramToolbar::setBreadcrumbs(const QStringList& labels)
{
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

void ModuleBlockDiagramToolbar::setHistoryAvailable(bool back, bool forward)
{
    backAction->setEnabled(back);
    forwardAction->setEnabled(forward);
}

void ModuleBlockDiagramToolbar::setHosted(bool hosted)
{
    fitButton->setVisible(!hosted);
}
