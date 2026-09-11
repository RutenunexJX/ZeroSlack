#ifndef SEMANTICCHANGECLASSIFIER_H
#define SEMANTICCHANGECLASSIFIER_H

#include "semanticanalysisrequest.h"

#include <QString>

class SemanticChangeClassifier
{
public:
    SemanticChangeClassification classify(
        const QString& fileName,
        const QString& oldText,
        const QString& newText) const;

    static SourceTextDelta textDelta(const QString& oldText,
                                     const QString& newText);
};

#endif // SEMANTICCHANGECLASSIFIER_H
