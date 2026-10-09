#pragma once
#include "model.h"
#include <functional>
namespace simdock
{
struct StimulusSignal
{
    QString name, type;
    int width = 1;
    bool isSigned = false;
    QMap<QString, QString> enumValues;
    bool namedType = false;
    QString direction;
};
// Value-only result. No Slang objects or source-manager pointers leave the worker.
struct StimulusSemantics
{
    QList<StimulusSignal> inputs, ports;
    QMap<QString, QByteArray> includeFingerprints;
    QString error, checksError;
};
StimulusSemantics resolveStimulus(const Module &, const Scan &,
                                  const std::function<bool()> &cancelled = {});
bool stimulusIncludesCurrent(const StimulusSemantics &, const std::function<bool()> &cancelled = {});
QJsonObject newStimulus(const Module &, const StimulusSemantics &, const TbOptions &, qint64, QString *);
QJsonObject retimeStimulus(const Module &, const StimulusSemantics &, const QJsonObject &, const TbOptions &,
                           qint64, QString *);
QString stimulusTestbench(const Module &, const StimulusSemantics &, const QJsonObject &, QString *);
bool saveGeneratedStimulus(const QString &root, Project &, const QJsonObject &, const QString &content,
                           QString *error);
Project generatedStimulusProject(Project, const QJsonObject &, const QString &content);
QList<StimulusSignal> stimulusSignals(const Module &, const Scan &, QString *error);
QList<StimulusSignal> scoreboardSignals(const Module &, const Scan &, QString *error);
QJsonObject newStimulus(const Module &, const Scan &, const TbOptions &, qint64 durationNs, QString *error);
QJsonObject retimeStimulus(const Module &, const Scan &, const QJsonObject &, const TbOptions &,
                           qint64 durationNs, QString *error);
TbOptions stimulusOptions(const QJsonObject &);
QString stimulusSignature(const Module &, const Scan &, QString *error);
QString stimulusTestbench(const Module &, const Scan &, const QJsonObject &, QString *error);
bool saveStimulus(const QString &root, Project &, const Module &, const Scan &, const QJsonObject &,
                  QString *error);
} // namespace simdock
