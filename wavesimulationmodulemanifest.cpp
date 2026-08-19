#include "wavesimulationmodulemanifest.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace {
QJsonArray stringArray(const QStringList& values)
{
    QJsonArray result;
    for (const QString& value : values)
        result.append(value);
    return result;
}

QJsonArray associationArray(
    const QList<WaveSimulationManifestAssociation>& associations)
{
    QJsonArray result;
    for (const WaveSimulationManifestAssociation& association : associations) {
        result.append(QJsonObject{
            {QStringLiteral("name"), association.name},
            {QStringLiteral("position"), association.position},
        });
    }
    return result;
}

QJsonObject unresolvedInstanceObject(
    const WaveSimulationManifestUnresolvedInstance& instance)
{
    return {
        {QStringLiteral("instanceName"), instance.instanceName},
        {QStringLiteral("constructKind"), instance.constructKind},
        {QStringLiteral("sourceFile"), instance.sourceFile},
        {QStringLiteral("sourceLine"), instance.sourceLine},
        {QStringLiteral("sourceColumn"), instance.sourceColumn},
        {QStringLiteral("parameterAssociations"),
         associationArray(instance.parameterAssociations)},
        {QStringLiteral("portAssociations"),
         associationArray(instance.portAssociations)},
        {QStringLiteral("syntaxComplete"), instance.syntaxComplete},
        {QStringLiteral("failureReason"), instance.failureReason},
    };
}

QJsonObject typeShapeObject(const WaveSimulationManifestTypeShape& shape)
{
    QJsonObject object;
    object.insert(QStringLiteral("semanticAvailable"), shape.semanticAvailable);
    object.insert(QStringLiteral("rawTypeText"), shape.rawTypeText);
    object.insert(QStringLiteral("resolvedTypeName"), shape.resolvedTypeName);
    object.insert(QStringLiteral("semanticKind"), shape.semanticKind);
    object.insert(QStringLiteral("resolvedTypeText"), shape.resolvedTypeText);
    object.insert(QStringLiteral("canonicalTypeId"), shape.canonicalTypeId);
    object.insert(QStringLiteral("declarationShapeId"), shape.declarationShapeId);
    object.insert(QStringLiteral("fixedSize"), shape.fixedSize);
    object.insert(QStringLiteral("integral"), shape.integral);
    object.insert(QStringLiteral("signed"), shape.signedIntegral);
    object.insert(QStringLiteral("unpackedArray"), shape.unpackedArray);
    object.insert(QStringLiteral("interfaceType"), shape.interfaceType);
    object.insert(QStringLiteral("bitWidth"),
                  static_cast<qint64>(shape.bitWidth));
    object.insert(QStringLiteral("packedDimensions"), shape.packedDimensions);
    object.insert(QStringLiteral("unpackedDimensions"), shape.unpackedDimensions);
    object.insert(QStringLiteral("unpackedElementCount"),
                  shape.unpackedElementCount);
    object.insert(QStringLiteral("interfaceName"), shape.interfaceName);
    object.insert(QStringLiteral("modportName"), shape.modportName);
    object.insert(QStringLiteral("typedefChain"), stringArray(shape.typedefChain));
    object.insert(QStringLiteral("failureReason"), shape.failureReason);
    return object;
}

QJsonObject typeObject(const WaveSimulationManifestType& type)
{
    QJsonObject object = typeShapeObject(type.shape);
    QJsonArray enumValues;
    for (const WaveSimulationManifestEnumValue& value : type.enumValues) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), value.name);
        entry.insert(QStringLiteral("declarationText"), value.declarationText);
        entry.insert(QStringLiteral("valueText"), value.valueText);
        entry.insert(QStringLiteral("displayValueText"), value.displayValueText);
        entry.insert(QStringLiteral("semanticAvailable"), value.semanticAvailable);
        enumValues.append(entry);
    }
    object.insert(QStringLiteral("enumValues"), enumValues);

    QJsonArray structMembers;
    for (const WaveSimulationManifestStructMember& member : type.structMembers) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), member.name);
        entry.insert(QStringLiteral("declarationText"), member.declarationText);
        entry.insert(QStringLiteral("type"), typeShapeObject(member.type));
        structMembers.append(entry);
    }
    object.insert(QStringLiteral("structMembers"), structMembers);
    return object;
}

QJsonObject editableLeafObject(
    const WaveSimulationManifestEditableLeaf& leaf)
{
    QJsonArray selectors;
    for (const WaveSimulationManifestStructuredSelector& selector :
         leaf.selectors) {
        selectors.append(QJsonObject{
            {QStringLiteral("kind"), selector.kind},
            {QStringLiteral("name"), selector.name},
            {QStringLiteral("sourceIndex"), selector.sourceIndex},
            {QStringLiteral("storageIndex"), selector.storageIndex},
        });
    }
    QJsonArray enumValues;
    for (const WaveSimulationManifestEnumValue& value : leaf.enumValues) {
        enumValues.append(QJsonObject{
            {QStringLiteral("name"), value.name},
            {QStringLiteral("declarationText"), value.declarationText},
            {QStringLiteral("valueText"), value.valueText},
            {QStringLiteral("displayValueText"), value.displayValueText},
            {QStringLiteral("semanticAvailable"), value.semanticAvailable},
        });
    }
    return {
        {QStringLiteral("relativePath"), leaf.relativePath},
        {QStringLiteral("direction"), leaf.direction},
        {QStringLiteral("selectors"), selectors},
        {QStringLiteral("type"), typeShapeObject(leaf.type)},
        {QStringLiteral("enumValues"), enumValues},
        {QStringLiteral("packedBitOffsetValid"), leaf.packedBitOffsetValid},
        {QStringLiteral("packedBitOffset"),
         static_cast<qint64>(leaf.packedBitOffset)},
    };
}
}

bool WaveSimulationModuleManifest::isValid() const
{
    const bool unresolvedValid = std::all_of(
        unresolvedDependencies.cbegin(),
        unresolvedDependencies.cend(),
        [](const WaveSimulationManifestUnresolvedDependency& dependency) {
            if (dependency.moduleName.isEmpty()
                || dependency.instances.isEmpty()
                || dependency.stubSupported
                       == !dependency.stubUnsupportedReason.isEmpty()) {
                return false;
            }
            return std::all_of(
                dependency.instances.cbegin(),
                dependency.instances.cend(),
                [](const WaveSimulationManifestUnresolvedInstance& instance) {
                    return !instance.constructKind.isEmpty()
                        && !instance.sourceFile.isEmpty()
                        && instance.sourceLine > 0
                        && instance.sourceColumn > 0;
                });
        });
    return schemaVersion == kSchemaVersion
        && workspaceId.startsWith(QStringLiteral("sha256:"))
        && !target.module.isEmpty()
        && !target.sourceFile.isEmpty()
        && (target.mode == QStringLiteral("module-definition")
            || (target.mode == QStringLiteral("instance")
                && !target.instancePath.isEmpty()))
        && (observationScope.mode == QStringLiteral("module")
            || (observationScope.mode == QStringLiteral("always")
                && !observationScope.sourceFile.isEmpty()
                && observationScope.startLine > 0
                && observationScope.endLine
                       >= observationScope.startLine))
        && unresolvedValid;
}

QJsonObject WaveSimulationModuleManifest::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);
    root.insert(QStringLiteral("workspaceId"), workspaceId);

    QJsonObject targetObject;
    targetObject.insert(QStringLiteral("mode"), target.mode);
    targetObject.insert(QStringLiteral("module"), target.module);
    targetObject.insert(QStringLiteral("instancePath"), target.instancePath);
    targetObject.insert(QStringLiteral("sourceFile"), target.sourceFile);
    targetObject.insert(QStringLiteral("sourceLine"), target.sourceLine);
    root.insert(QStringLiteral("target"), targetObject);

    QJsonObject scopeObject;
    scopeObject.insert(QStringLiteral("mode"), observationScope.mode);
    scopeObject.insert(QStringLiteral("label"), observationScope.label);
    scopeObject.insert(QStringLiteral("sourceFile"),
                       observationScope.sourceFile);
    scopeObject.insert(QStringLiteral("startLine"),
                       observationScope.startLine);
    scopeObject.insert(QStringLiteral("endLine"),
                       observationScope.endLine);
    root.insert(QStringLiteral("observationScope"), scopeObject);

    QJsonArray observationArray;
    for (const WaveSimulationManifestObservation& observation :
         observations) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), observation.name);
        entry.insert(QStringLiteral("accessPath"), observation.accessPath);
        entry.insert(QStringLiteral("semanticId"), observation.semanticId);
        entry.insert(QStringLiteral("declarationText"),
                     observation.declarationText);
        entry.insert(QStringLiteral("type"), typeObject(observation.type));
        entry.insert(QStringLiteral("sourceFile"), observation.sourceFile);
        entry.insert(QStringLiteral("sourceLine"), observation.sourceLine);
        entry.insert(QStringLiteral("port"), observation.port);
        observationArray.append(entry);
    }
    root.insert(QStringLiteral("observations"), observationArray);

    QJsonArray sourceArray;
    for (const WaveSimulationManifestSource& source : sources) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), source.path);
        entry.insert(QStringLiteral("role"), source.role);
        sourceArray.append(entry);
    }
    root.insert(QStringLiteral("sources"), sourceArray);

    QJsonArray unresolvedArray;
    for (const WaveSimulationManifestUnresolvedDependency& dependency :
         unresolvedDependencies) {
        QJsonArray instances;
        for (const WaveSimulationManifestUnresolvedInstance& instance :
             dependency.instances) {
            instances.append(unresolvedInstanceObject(instance));
        }
        unresolvedArray.append(QJsonObject{
            {QStringLiteral("moduleName"), dependency.moduleName},
            {QStringLiteral("instances"), instances},
            {QStringLiteral("stubSupported"), dependency.stubSupported},
            {QStringLiteral("stubUnsupportedReason"),
             dependency.stubUnsupportedReason},
        });
    }
    root.insert(QStringLiteral("unresolvedDependencies"), unresolvedArray);
    root.insert(QStringLiteral("includeDirs"), stringArray(includeDirs));

    QJsonObject defineObject;
    for (auto it = defines.cbegin(); it != defines.cend(); ++it)
        defineObject.insert(it.key(), it.value());
    root.insert(QStringLiteral("defines"), defineObject);

    QJsonObject parameterObject;
    for (const WaveSimulationManifestParameter& parameter : parameters) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), parameter.name);
        entry.insert(QStringLiteral("declarationText"),
                     parameter.declarationText);
        entry.insert(QStringLiteral("expressionText"), parameter.expressionText);
        entry.insert(QStringLiteral("valueText"), parameter.valueText);
        entry.insert(QStringLiteral("displayValueText"),
                     parameter.displayValueText);
        entry.insert(QStringLiteral("semanticAvailable"),
                     parameter.semanticAvailable);
        entry.insert(QStringLiteral("type"), typeObject(parameter.type));
        entry.insert(QStringLiteral("sourceFile"), parameter.sourceFile);
        entry.insert(QStringLiteral("sourceLine"), parameter.sourceLine);
        parameterObject.insert(parameter.name, entry);
    }
    root.insert(QStringLiteral("parameters"), parameterObject);

    QJsonArray portArray;
    for (const WaveSimulationManifestPort& port : ports) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), port.name);
        entry.insert(QStringLiteral("direction"), port.direction);
        entry.insert(QStringLiteral("declarationText"), port.declarationText);
        entry.insert(QStringLiteral("type"), typeObject(port.type));
        entry.insert(QStringLiteral("structuredLeavesAvailable"),
                     port.structuredLeavesAvailable);
        QJsonArray leaves;
        for (const WaveSimulationManifestEditableLeaf& leaf :
             port.editableLeaves) {
            leaves.append(editableLeafObject(leaf));
        }
        entry.insert(QStringLiteral("editableLeaves"), leaves);
        entry.insert(QStringLiteral("structuredFailureReason"),
                     port.structuredFailureReason);
        entry.insert(QStringLiteral("sourceFile"), port.sourceFile);
        entry.insert(QStringLiteral("sourceLine"), port.sourceLine);
        portArray.append(entry);
    }
    root.insert(QStringLiteral("ports"), portArray);
    root.insert(QStringLiteral("clockCandidates"), stringArray(clockCandidates));
    root.insert(QStringLiteral("resetCandidates"), stringArray(resetCandidates));
    return root;
}

QByteArray WaveSimulationModuleManifest::toJsonBytes() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
}
