#ifndef WAVESIMULATIONMODULEMANIFEST_H
#define WAVESIMULATIONMODULEMANIFEST_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <cstdint>

struct WaveSimulationManifestSource {
    QString path;
    QString role;
};

struct WaveSimulationManifestAssociation {
    QString name;
    int position = 0;
};

struct WaveSimulationManifestUnresolvedInstance {
    QString instanceName;
    QString constructKind;
    QString sourceFile;
    int sourceLine = 0;
    int sourceColumn = 0;
    QList<WaveSimulationManifestAssociation> parameterAssociations;
    QList<WaveSimulationManifestAssociation> portAssociations;
    bool syntaxComplete = false;
    QString failureReason;
};

struct WaveSimulationManifestUnresolvedDependency {
    QString moduleName;
    QList<WaveSimulationManifestUnresolvedInstance> instances;
    bool stubSupported = false;
    QString stubUnsupportedReason;
};

struct WaveSimulationManifestTypeShape {
    bool semanticAvailable = false;
    QString rawTypeText;
    QString resolvedTypeName;
    QString semanticKind;
    QString resolvedTypeText;
    QString canonicalTypeId;
    QString declarationShapeId;
    bool fixedSize = false;
    bool integral = false;
    bool signedIntegral = false;
    bool unpackedArray = false;
    bool interfaceType = false;
    std::uint64_t bitWidth = 0;
    QString packedDimensions;
    QString unpackedDimensions;
    QString unpackedElementCount;
    QString interfaceName;
    QString modportName;
    QStringList typedefChain;
    QString failureReason;
};

struct WaveSimulationManifestEnumValue {
    QString name;
    QString declarationText;
    QString valueText;
    QString displayValueText;
    bool semanticAvailable = false;
};

struct WaveSimulationManifestStructMember {
    QString name;
    QString declarationText;
    WaveSimulationManifestTypeShape type;
};

struct WaveSimulationManifestStructuredSelector {
    QString kind;
    QString name;
    int sourceIndex = 0;
    int storageIndex = 0;
};

struct WaveSimulationManifestEditableLeaf {
    QString relativePath;
    QString direction;
    QList<WaveSimulationManifestStructuredSelector> selectors;
    WaveSimulationManifestTypeShape type;
    QList<WaveSimulationManifestEnumValue> enumValues;
    bool packedBitOffsetValid = false;
    std::uint64_t packedBitOffset = 0;
};

struct WaveSimulationManifestType {
    WaveSimulationManifestTypeShape shape;
    QList<WaveSimulationManifestEnumValue> enumValues;
    QList<WaveSimulationManifestStructMember> structMembers;
};

struct WaveSimulationManifestParameter {
    QString name;
    QString declarationText;
    QString expressionText;
    QString valueText;
    QString displayValueText;
    bool semanticAvailable = false;
    WaveSimulationManifestType type;
    QString sourceFile;
    int sourceLine = 0;
};

struct WaveSimulationManifestPort {
    QString name;
    QString direction;
    QString declarationText;
    WaveSimulationManifestType type;
    bool structuredLeavesAvailable = false;
    QList<WaveSimulationManifestEditableLeaf> editableLeaves;
    QString structuredFailureReason;
    QString sourceFile;
    int sourceLine = 0;
};

struct WaveSimulationManifestTarget {
    QString mode;
    QString module;
    QString instancePath;
    QString sourceFile;
    int sourceLine = 0;
};

struct WaveSimulationManifestObservationScope {
    QString mode = QStringLiteral("module");
    QString label;
    QString sourceFile;
    int startLine = 0;
    int endLine = 0;
};

struct WaveSimulationManifestObservation {
    QString name;
    QString accessPath;
    QString semanticId;
    QString declarationText;
    WaveSimulationManifestType type;
    QString sourceFile;
    int sourceLine = 0;
    bool port = false;
};

struct WaveSimulationModuleManifest {
    static constexpr int kSchemaVersion = 4;

    int schemaVersion = kSchemaVersion;
    QString workspaceId;
    WaveSimulationManifestTarget target;
    WaveSimulationManifestObservationScope observationScope;
    QList<WaveSimulationManifestObservation> observations;
    QList<WaveSimulationManifestSource> sources;
    QList<WaveSimulationManifestUnresolvedDependency> unresolvedDependencies;
    QStringList includeDirs;
    QMap<QString, QString> defines;
    QList<WaveSimulationManifestParameter> parameters;
    QList<WaveSimulationManifestPort> ports;
    QStringList clockCandidates;
    QStringList resetCandidates;

    bool isValid() const;
    QJsonObject toJson() const;
    QByteArray toJsonBytes() const;
};

#endif // WAVESIMULATIONMODULEMANIFEST_H
