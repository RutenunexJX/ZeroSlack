#pragma once
#include "zeroslackexport.h"
#include "../simulation/simdock/core/workspacestatefile.h"
#include <zeroslack/semantic/hierarchyservice.h>
#include <QObject>

class ZEROSLACK_API DesignProjectService final : public QObject {
    Q_OBJECT
public:
    explicit DesignProjectService(QObject* parent = nullptr);
    QString setWorkspace(const QString& root);
    QString workspace() const { return m_root; }
    QList<DesignHierarchyRoot> designRoots() const { return m_roots; }
    QString activeProjectId() const { return m_active; }
    QString activateProject(const QString& id);
    QString addDesignProject(const QString& file, const QString& module, bool activate = true);
    QString removeDesignRoot(const QString& id);
    QString setDesignExpanded(const QString& id, bool expanded);
    bool designMigrationCompleted() const { return m_explicitTopMigrated; }
    QString markDesignMigrationCompleted();
signals:
    void designProjectsChanged();
    void designExpansionChanged(const QString& id, bool expanded);
private:
    QString commit(const QList<DesignHierarchyRoot>& roots, const QString& active, bool migrated);
    QString m_root, m_active;
    QList<DesignHierarchyRoot> m_roots;
    bool m_explicitTopMigrated = false;
    simdock::WorkspaceStateFile m_store;
};
