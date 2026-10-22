#pragma once
#include <QList>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QByteArray>

namespace simdock {
struct Port {
    QString name, direction, type;
    bool namedType = false;
    QString initialExpression;
    quint64 bitWidth = 0;
};
struct Parameter {
    QString name, type, value;
    bool local = false;
};
struct Module {
    QString name, file;
    int line = 1;
    QList<Port> ports;
    QList<Parameter> parameters;
    QStringList limitations;
    QStringList imports;
};
struct SourceFile {
    QString path;
    QList<Module> modules;
    QStringList includes;
    QStringList declaredUnits, instantiatedUnits, declaredPackages, referencedPackages;
    QByteArray content; // Immutable UTF-8 source snapshot used by Slang.
    bool syntaxError = false;
    bool dynamicInclude = false;
};
struct Scan {
    QString root;
    QList<SourceFile> files;
    QStringList messages;
};
enum class InputMode { Graphical, ExistingTb };
struct InputConfiguration {
    QString tbFile, tbName;
    qint64 durationNs = 1000;
    QJsonObject stimulus;
};
struct Project {
    QString id, name;
    QString designTopFile, designTopModule;
    QStringList sources;
    QStringList includeDirectories;
    QString dutFile, dutName, tbFile, tbName;
    qint64 durationNs = 1000;
    QJsonObject stimulus;
    InputMode inputMode = InputMode::ExistingTb;
    InputConfiguration alternateInput;
    // Legacy selection metadata is preserved; every run records all hierarchy signals.
    QString waveScope = QStringLiteral("all");
    QStringList waveSignals;
    // Retired graphical inputs remain serializable for data compatibility only.
    QJsonObject preservedInputs;
    QString storageName, legacyFile;
    QMap<QString, QByteArray> storageRevisions;
    // Derived only for a prepared run. These are not a second saved selection.
    QStringList compilationUnits, compilationIncludeDirectories;
    QHash<QString, QString> compilationDefines;
};
struct TbOptions {
    QString name, clock, reset;
    bool resetActiveLow = true;
    int clockPeriodNs = 10;
    int resetCycles = 5;
};
}
