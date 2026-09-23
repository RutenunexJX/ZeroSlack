#include "moduleblockdiagramtoolbar.h"

#include "uicontrols.h"

#include <QAction>
#include <QHBoxLayout>
#include <QToolButton>

ModuleBlockDiagramToolbar::ModuleBlockDiagramToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("rtlModuleBlockToolbar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch();
    fitAction = new QAction(tr("Fit"), this);
    fitAction->setObjectName(QStringLiteral("rtlModuleFitAction"));
    fitAction->setToolTip(tr("Fit diagram to view"));
    auto* fit = UiControls::toolButton(this);
    fit->setDefaultAction(fitAction);
    fit->setToolButtonStyle(Qt::ToolButtonTextOnly);
    fit->setAutoRaise(true);
    row->addWidget(fit);
    hide();
}

void ModuleBlockDiagramToolbar::setModuleMode(bool active)
{
    moduleMode = active;
    setVisible(moduleMode && !hosted);
}

void ModuleBlockDiagramToolbar::setHosted(bool value)
{
    hosted = value;
    setVisible(moduleMode && !hosted);
}
