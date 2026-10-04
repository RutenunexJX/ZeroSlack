#ifndef SEMANTICCHANGECLASSIFIER_H
#define SEMANTICCHANGECLASSIFIER_H

#include "semanticanalysisrequest.h"

#include <QString>
#include <functional>

class SemanticChangeClassifier
{
public:
    SemanticChangeClassification classify(
        const QString& fileName,
        const QString& oldText,
        const QString& newText, const std::function<bool()>& cancelled = {}) const;

    static SourceTextDelta textDelta(const QString& oldText,
                                     const QString& newText);
};

#endif // SEMANTICCHANGECLASSIFIER_H
