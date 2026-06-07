#ifndef MODULEHIERARCHYMODEL_H
#define MODULEHIERARCHYMODEL_H

#include <QString>
#include <QStringList>

enum class ModuleHierarchyRootKind {
    FileGroup,
    ModuleRoot
};

struct ModuleHierarchyGroup {
    ModuleHierarchyRootKind rootKind = ModuleHierarchyRootKind::FileGroup;
    QString rootName;
    QString rootDisplayName;
    QString rootToolTip;
    QStringList childModules;
};

#endif // MODULEHIERARCHYMODEL_H
