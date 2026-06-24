#include "diagnosticsrefreshcontroller.h"

#include <QTimer>

DiagnosticsRefreshController::DiagnosticsRefreshController(QObject* parent)
    : QObject(parent)
{
    refreshTimer = new QTimer(this);
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(100);
    connect(refreshTimer, &QTimer::timeout, this, [this]() {
        const QString fileName = pendingFullRefresh
            ? QString()
            : pendingFileName;
        pendingFileName.clear();
        pendingFullRefresh = false;
        emit diagnosticsRefreshRequested(fileName);
    });
}

void DiagnosticsRefreshController::requestRefresh(const QString& fileName)
{
    if (fileName.isEmpty()) {
        pendingFullRefresh = true;
        pendingFileName.clear();
    } else if (!pendingFullRefresh) {
        pendingFileName = fileName;
    }
    refreshTimer->start();
}
