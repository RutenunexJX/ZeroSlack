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
    QPair<QString, QString> sourceTop() const { return m_sourceTop; }
    QString setSourceTop(const QString& file, const QString& module);
    QString workspace() const { return m_root; }
    QList<DesignHierarchyRoot> designRoots() const { return m_roots; }
    QString activeProjectId() const { return m_active; }
    QString activateProject(const QString& id);
    QString addDesignProject(const QString& file, const QString& module, bool activate = true);
    QString removeDesignRoot(const QString& id);
    QString setDesignExpanded(const QString& id, bool expanded);
    bool designMigrationCompleted() const { return m_explicitTopMigrated; }
    QString markDesignMigrationCompleted();
    QJsonObject viewState() const { return m_store.value().value("viewState").toObject(); }
    QString saveViewState(const QString& workspace, const QJsonObject& state);
signals:
    void designProjectsChanged();
    void designExpansionChanged(const QString& id, bool expanded);
private:
    QString commit(const QList<DesignHierarchyRoot>& roots, const QString& active, bool migrated);
    QString m_root, m_active;
    QPair<QString, QString> m_sourceTop;
    QList<DesignHierarchyRoot> m_roots;
    bool m_explicitTopMigrated = false;
    simdock::WorkspaceStateFile m_store;
};
