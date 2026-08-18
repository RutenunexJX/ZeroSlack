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

struct WaveSimulationModuleManifest {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    QString workspaceId;
    WaveSimulationManifestTarget target;
    QList<WaveSimulationManifestSource> sources;
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
