#ifndef DIAGNOSTICSREFRESHCONTROLLER_H
#define DIAGNOSTICSREFRESHCONTROLLER_H

#include "zeroslackexport.h"

#include <QObject>
#include <QString>

class ZEROSLACK_API DiagnosticsRefreshController : public QObject
{
    Q_OBJECT

public:
    explicit DiagnosticsRefreshController(QObject* parent = nullptr);

    void requestRefresh(const QString& fileName);

signals:
    void diagnosticsRefreshRequested(const QString& fileName);

private:
    QString pendingFileName;
    bool pendingFullRefresh = false;
    bool refreshQueued = false;
};

#endif // DIAGNOSTICSREFRESHCONTROLLER_H
