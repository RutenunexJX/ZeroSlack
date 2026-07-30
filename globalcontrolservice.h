#ifndef GLOBALCONTROLSERVICE_H
#define GLOBALCONTROLSERVICE_H

#include <QString>
#include <QList>

enum class GlobalControlItemKind {
    Domain,
    Command
};

struct GlobalControlItem {
    GlobalControlItemKind kind = GlobalControlItemKind::Command;
    QString id;
    QString title;
    QString subtitle;
    QString actionId;
    QString executionRoute;
};

class GlobalControlService
{
public:
    QList<GlobalControlItem> query(const QString& text) const;
};

#endif // GLOBALCONTROLSERVICE_H
