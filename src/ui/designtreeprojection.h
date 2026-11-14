#pragma once
#include "hierarchyservice.h"
#include "projectsnapshot.h"
#include "zeroslackexport.h"

// UI-only rows. File/category identities never enter the semantic instance graph.
struct DesignTreeRow {
    QString id, parentId, name, type, file, status;
    DesignHierarchyNode semantic;
    bool category = false, source = false, simulation = false, unresolved = false;
};
struct ZEROSLACK_API DesignTreeProjection {
    QList<DesignTreeRow> nodes;
    QHash<QString, int> nodeById;
    QHash<QString, QList<int>> childrenById;
    QHash<QString, QString> firstPresentationBySemanticId;
    QByteArray structureFingerprint;
    void append(DesignTreeRow row);
    static std::shared_ptr<DesignTreeProjection> hierarchy(const DesignHierarchyReport& report);
    static std::shared_ptr<DesignTreeProjection> build(const DesignHierarchyReport& report,
        const SemanticSnapshotToken& token, const ProjectSnapshot& project,
        const QPair<QString, QString>& source, const QPair<QString, QString>& simulation,
        const std::function<bool()>& cancelled);
};
