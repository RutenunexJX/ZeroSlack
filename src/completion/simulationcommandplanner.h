#pragma once
#include "globalcontrolservice.h"
#include "semanticanalysisinput.h"
#include "templateinputservice.h"
#include "documenttemplatemodel.h"
#include <atomic>

struct SimulationCommandRequest
{
    GlobalControlQueryContext context;
    GlobalControlItem item;
    QStringList orderedFiles, includeDirectories;
    QHash<QString, QString> contents, defines;
    QVariantMap roles;
    std::shared_ptr<std::atomic_bool> cancelled;
};
struct SimulationCommandPlan
{
    QString error, text;
    int start = -1, length = 0;
    CodeTemplateSlotList placeholders;
    TemplateInsertionSpec instance;
    std::shared_ptr<TemplateInputContext> inputs;
    std::shared_ptr<const SemanticAnalysisInput> captured;
};
enum class SimulationPlacementStatus { Applicable, Incompatible, Unavailable };
struct SimulationPlacement {
    SimulationPlacementStatus status = SimulationPlacementStatus::Applicable;
    QString reason;
};
class SimulationCommandPlanner
{
  public:
    static SimulationCommandPlan prepare(const SimulationCommandRequest &request);
    static QString stringLiteral(const QString &value);
    static bool sourcesCurrent(const std::shared_ptr<const SemanticAnalysisInput> &input);
    static SimulationPlacement placement(const GlobalControlQueryContext& context, const QVariantMap& command,
                                          const QString& content = {}, const QString& snippet = {});
    static QString placementError(const GlobalControlQueryContext& context, const QVariantMap& command,
                                  const QString& content = {}, const QString& snippet = {});
};
