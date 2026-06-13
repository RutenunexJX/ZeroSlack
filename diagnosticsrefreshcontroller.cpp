#include "diagnosticsrefreshcontroller.h"

#include <QTimer>

DiagnosticsRefreshController::DiagnosticsRefreshController(QObject* parent)
    : QObject(parent)
{
    refreshTimer = new QTimer(this);
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(100);
    connect(refreshTimer, &QTimer::timeout, this, [this]() {
        const QString fileName = pendingFileName;
        pendingFileName.clear();
        emit diagnosticsRefreshRequested(fileName);
    });
}

void DiagnosticsRefreshController::requestRefresh(const QString& fileName)
{
    pendingFileName = fileName;
    refreshTimer->start();
}
