#ifndef SYMBOLPRESENTATIONSERVICE_H
#define SYMBOLPRESENTATIONSERVICE_H

#include <zeroslack/semantic/semanticindex.h>

#include <QString>

struct HierarchyInstanceContext {
    QString workspacePath;
    QString activeTopModule;
    QString instancePath;
    QString rootIdentity;
    QString definitionIdentity;
    QString configurationIdentity;

    bool isBound() const
    {
        return !workspacePath.isEmpty()
            && !activeTopModule.isEmpty()
            && !instancePath.isEmpty();
    }

    bool operator==(const HierarchyInstanceContext& other) const
    {
        return workspacePath == other.workspacePath
            && activeTopModule == other.activeTopModule
            && instancePath == other.instancePath
            && rootIdentity == other.rootIdentity
            && definitionIdentity == other.definitionIdentity
            && configurationIdentity == other.configurationIdentity;
    }
};

class SymbolPresentationService
{
public:
    // Presentation text is produced together with the Slang semantic
    // snapshot. Consumers must never parse an editor buffer synchronously.
    static SemanticSymbolPresentation presentationForRecord(
        const SemanticSymbolRecord& record);
};

Q_DECLARE_METATYPE(HierarchyInstanceContext)

#endif // SYMBOLPRESENTATIONSERVICE_H
