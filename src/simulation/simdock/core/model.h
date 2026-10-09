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
    QStringList sources;
    QString dutFile, dutName, tbFile, tbName;
    qint64 durationNs = 1000;
    QJsonObject stimulus;
    InputMode inputMode = InputMode::ExistingTb;
    InputConfiguration alternateInput;
    QString waveScope = QStringLiteral("interface");
    QStringList waveSignals;
};
inline void setInputMode(Project& project, InputMode mode)
{
    if (project.inputMode == mode) return;
    InputConfiguration previous{project.tbFile, project.tbName, project.durationNs, project.stimulus};
    project.tbFile = project.alternateInput.tbFile;
    project.tbName = project.alternateInput.tbName;
    project.durationNs = project.alternateInput.durationNs;
    project.stimulus = project.alternateInput.stimulus;
    project.alternateInput = previous;
    project.inputMode = mode;
}
struct TbOptions {
    QString name, clock, reset;
    bool resetActiveLow = true;
    int clockPeriodNs = 10;
    int resetCycles = 5;
};
}
