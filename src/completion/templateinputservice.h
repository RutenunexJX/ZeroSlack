#pragma once
#include "completiontypes.h"
#include "semanticindexsnapshot.h"
#include <functional>

struct TemplateInputContext
{
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    SemanticQueryContext location;
    QVariantMap roles;
    std::function<bool()> current;
};
struct TemplateInputCandidates
{
    QStringList values;
    QString reason;
};
class TemplateInputService
{
  public:
    static QString specificationError(const QVariantMap &input);
    static QString dependencyError(const CodeTemplateSlotList &placeholders);
    static TemplateInputCandidates candidates(const QVariantMap &input, const TemplateInputContext &context,
                                              const QHash<QString, QString> &bindings);
    static QString validate(const QVariantMap &input, const QString &text,
                            const TemplateInputContext &context, const QHash<QString, QString> &bindings);
    static QString roleKey(const SemanticSymbolRecord &record, const QString &objectPath);
};
