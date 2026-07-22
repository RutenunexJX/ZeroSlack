#ifndef SEMANTICSOURCEREMAP_H
#define SEMANTICSOURCEREMAP_H

#include "effectivevalueservice.h"
#include "semanticanalysisrequest.h"

#include <QList>
#include <QString>
#include <memory>

class SemanticIndexSnapshot;

class SemanticSourceRemapper
{
public:
    static std::shared_ptr<const SemanticIndexSnapshot> remapSnapshot(
        std::shared_ptr<const SemanticIndexSnapshot> snapshot,
        const QString& fileName,
        const QString& oldText,
        const QString& newText,
        const SourceTextDelta& delta,
        std::uint64_t documentRevision = 0);

    static QList<EffectiveValueFact> remapEffectiveFacts(
        QList<EffectiveValueFact> facts,
        const QString& fileName,
        const QString& oldText,
        const QString& newText,
        const SourceTextDelta& delta,
        std::uint64_t documentRevision = 0);
};

#endif // SEMANTICSOURCEREMAP_H
