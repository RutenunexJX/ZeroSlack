#pragma once
#include "simulationcommandplanner.h"
#include <QSet>

// Source-owned syntax, captured once; no Slang objects cross the worker boundary.
struct ModuleTemplateMember {
    QString name, type, dimensions, value;
    bool local = false, typeParameter = false;
};
struct ModuleTemplateModel {
    QString name, file, error, portsError;
    QList<ModuleTemplateMember> parameters, ports;
    std::shared_ptr<const SemanticAnalysisInput> captured;
};
struct ModuleTemplateOptions {
    QString mode = "signals", prefix, suffix, parameterKeyword = "localparam";
    bool includeParameters = true;
    QSet<QString> selectedParameters;
    QHash<QString, QString> values;
};
struct ModuleTemplatePlan {
    QString text, error;
    QStringList requiredParameters;
};

class ModuleTemplatePlanner {
public:
    static bool handles(const QVariantMap& configuration);
    static QString configurationError(const QVariantMap& configuration);
    static ModuleTemplateModel inspect(const SimulationCommandRequest& source,
                                       const SemanticSymbolRecord& module);
    static ModuleTemplatePlan prepare(const ModuleTemplateModel& model,
                                       const ModuleTemplateOptions& options,
                                       const QSet<QString>& occupied = {});
    static QString validateInsertion(const SimulationCommandRequest& source,
                                      const ModuleTemplateModel& model,
                                      const QString& text, bool validateSemantics = true);
};
