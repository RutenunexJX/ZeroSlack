#include "relationshipprogressdialog.h"

RelationshipProgressDialog::RelationshipProgressDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setupConnections();

    setModal(true);
    setWindowTitle("Symbol Relationship Analysis Progress");
    setMinimumSize(500, 200);
    resize(600, 300);

    timeUpdateTimer = nullptr;
    estimationTimer = nullptr;
}

RelationshipProgressDialog::~RelationshipProgressDialog()
{
    if (timeUpdateTimer) {
        timeUpdateTimer->stop();
        timeUpdateTimer->deleteLater();
        timeUpdateTimer = nullptr;
    }

    if (estimationTimer) {
        estimationTimer->stop();
        estimationTimer->deleteLater();
        estimationTimer = nullptr;
    }
}

void RelationshipProgressDialog::setShowDetails(bool show)
{
    config.showDetails = show;
    detailsButton->setChecked(show);
    onDetailsToggled(show);
}

void RelationshipProgressDialog::setAutoClose(bool autoClose)
{
    config.autoClose = autoClose;
}

void RelationshipProgressDialog::setMinimumDuration(int msecs)
{
    config.minimumDuration = msecs;
}

void RelationshipProgressDialog::forceShow()
{
    show();
    raise();
    activateWindow();
    setWindowState(windowState() & ~Qt::WindowMinimized);

    if (parentWidget()) {
        move(parentWidget()->geometry().center() - rect().center());
    }
}

void RelationshipProgressDialog::debugState() const
{
}
