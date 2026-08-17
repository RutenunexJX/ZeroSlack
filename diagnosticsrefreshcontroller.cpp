#include "diagnosticsrefreshcontroller.h"

DiagnosticsRefreshController::DiagnosticsRefreshController(QObject* parent)
    : QObject(parent)
{
}

void DiagnosticsRefreshController::requestRefresh(const QString& fileName)
{
    if (fileName.isEmpty()) {
        pendingFullRefresh = true;
        pendingFileName.clear();
    } else if (!pendingFullRefresh && pendingFileName.isEmpty()) {
        pendingFileName = fileName;
    } else if (!pendingFullRefresh && pendingFileName != fileName) {
        pendingFullRefresh = true;
        pendingFileName.clear();
    }
    if (refreshQueued)
        return;
    refreshQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            refreshQueued = false;
            const QString fileName = pendingFullRefresh
                ? QString()
                : pendingFileName;
            pendingFileName.clear();
            pendingFullRefresh = false;
            emit diagnosticsRefreshRequested(fileName);
        },
        Qt::QueuedConnection);
}
