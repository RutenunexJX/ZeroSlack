#ifndef CONTEXTRAIL_H
#define CONTEXTRAIL_H

#include "zeroslackexport.h"

#include <QHash>
#include <QIcon>
#include <QStringList>
#include <QToolBar>
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaToolBar.h"
using ContextRailBase = ElaToolBar;
#else
using ContextRailBase = QToolBar;
#endif

class QAction;

struct ContextRailEntry {
    QString id;
    QString title;
    QString toolTip;
    QIcon icon;
};

class ZEROSLACK_API ContextRail final : public ContextRailBase
{
    Q_OBJECT

public:
    explicit ContextRail(QWidget* parent = nullptr);

    QAction* addRailAction(const QIcon& icon, const QString& title, QAction* before = nullptr);
    bool addEntry(const ContextRailEntry& entry);
    bool removeEntry(const QString& id);
    bool setEntryIcon(const QString& id, const QIcon& icon);
    bool setEntryVisible(const QString& id, bool visible);
    void clearEntries();
    QStringList entryIds() const;

    QString activeEntryId() const;
    void setActiveEntryId(const QString& id);

signals:
    void entryActivated(const QString& id);
    void entryContextMenuRequested(const QString& id, const QPoint& globalPos);

private:
    QHash<QString, QAction*> actionsById;
    QString activeId;

    void updateVisibility();
};

#endif // CONTEXTRAIL_H
