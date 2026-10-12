#pragma once
#include <QObject>
#include <QPointer>

class TabManager;
class WorkspaceManager;
class QFrame;

class TbEditorWorkflow : public QObject {
    Q_OBJECT
public:
    TbEditorWorkflow(TabManager* tabs, WorkspaceManager* workspace, QObject* parent);
    void create(const QString& dutFile = {}, const QString& dutName = {});
    void cancel();
signals:
    void testbenchCreated(const QString& dutFile, const QString& dutName,
                          const QString& tbFile, const QString& tbName);
private:
    TabManager* tabs;
    WorkspaceManager* workspace;
    QPointer<QFrame> popup;
};
