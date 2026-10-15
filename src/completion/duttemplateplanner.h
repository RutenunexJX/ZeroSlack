#pragma once
#include "simulationcommandplanner.h"
#include <QSet>

struct DutTemplateOptions {
    QString module, moduleFile, instance, replaceId;
    QHash<QString, QString> parameters, bindings;
    QSet<QString> reuse;
};
struct DutTemplatePort {
    QString name, direction, type, suffix;
    QStringList compatibleSignals;
};
struct DutTemplateAnalysis {
    QString error, instance;
    QStringList modules, parameters;
    QHash<QString, QString> defaults;
    QList<DutTemplatePort> ports;
    QSet<QString> names;
    QList<SemanticSymbolRecord> records;
    std::shared_ptr<const SemanticAnalysisInput> captured;
};
struct DutTemplatePlan {
    QString error;
    QList<TemplateDocumentEdit> edits;
    std::shared_ptr<const SemanticAnalysisInput> captured;
};
class DutTemplatePlanner {
public:
    static QString rootError(const TemplateInstance &root);
    static QString availableName(const QString &base, const QSet<QString> &names);
    static DutTemplateAnalysis analyze(const SimulationCommandRequest &request,
        const QList<TemplateInstance> &instances, const DutTemplateOptions &options);
    static DutTemplatePlan prepare(const SimulationCommandRequest &request,
        const QList<TemplateInstance> &instances, const DutTemplateOptions &options);
};
