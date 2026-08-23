#ifndef CONTEXTRAIL_H
#define CONTEXTRAIL_H

#include "zeroslackexport.h"

#include <QHash>
#include <QIcon>
#include <QStringList>
#include <QToolBar>

class QAction;

struct ContextRailEntry {
    QString id;
    QString title;
    QString toolTip;
    QIcon icon;
};

class ZEROSLACK_API ContextRail final : public QToolBar
{
    Q_OBJECT

public:
    explicit ContextRail(QWidget* parent = nullptr);

    bool addEntry(const ContextRailEntry& entry);
    bool removeEntry(const QString& id);
    void clearEntries();
    QStringList entryIds() const;

    QString activeEntryId() const;
    void setActiveEntryId(const QString& id);

signals:
    void entryActivated(const QString& id);

private:
    QHash<QString, QAction*> actionsById;
    QString activeId;

    void updateVisibility();
};

#endif // CONTEXTRAIL_H
